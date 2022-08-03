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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#include "mm-helpers.h"
#include "mm-sim.h"
#include "mm-sim-preferred-network.h"

/**
 * SECTION: mm-sim
 * @title: MMSim
 * @short_description: The SIM interface
 *
 * The #MMSim is an object providing access to the methods, signals and
 * properties of the SIM interface.
 *
 * When the SIM is exposed and available in the bus, it is ensured that at
 * least this interface is also available.
 */

G_DEFINE_TYPE (MMSim, mm_sim, MM_GDBUS_TYPE_SIM_PROXY)

/*****************************************************************************/

/**
 * mm_sim_get_path:
 * @self: A #MMSim.
 *
 * Gets the DBus path of the #MMSim object.
 *
 * Returns: (transfer none): The DBus path of the #MMSim object.
 *
 * Since: 1.0
 */
const gchar *
mm_sim_get_path (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_sim_dup_path:
 * @self: A #MMSim.
 *
 * Gets a copy of the DBus path of the #MMSim object.
 *
 * Returns: (transfer full): The DBus path of the #MMSim object. The returned
 * value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_sim_dup_path (MMSim *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_sim_get_active:
 * @self: A #MMSim.
 *
 * Checks whether the #MMSim is currently active.
 *
 * Returns: %TRUE if the SIM is active, %FALSE otherwise.
 *
 * Since: 1.16
 */
gboolean
mm_sim_get_active (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return mm_gdbus_sim_get_active (MM_GDBUS_SIM (self));
}

/*****************************************************************************/

/**
 * mm_sim_get_identifier:
 * @self: A #MMSim.
 *
 * Gets the unique SIM identifier of the #MMSim object.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_sim_dup_identifier() if on another thread.</warning>
 *
 * Returns: (transfer none): The unique identifier of the #MMSim object, or
 * %NULL if it couldn't be retrieved.
 *
 * Since: 1.0
 */
const gchar *
mm_sim_get_identifier (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sim_get_sim_identifier (MM_GDBUS_SIM (self)));
}

/**
 * mm_sim_dup_identifier:
 * @self: A #MMSim.
 *
 * Gets a copy of the unique SIM identifier of the #MMSim object.
 *
 * Returns: (transfer full): The unique identifier of the #MMSim object, or
 * %NULL if it couldn't be retrieved. The returned value should be freed with
 * g_free().
 *
 * Since: 1.0
 */
gchar *
mm_sim_dup_identifier (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sim_dup_sim_identifier (MM_GDBUS_SIM (self)));
}

/*****************************************************************************/

/**
 * mm_sim_get_imsi:
 * @self: A #MMSim.
 *
 * Gets the International Mobile Subscriber Identity (IMSI) of the #MMSim
 * object.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_sim_dup_imsi() if on another thread.</warning>
 *
 * Returns: (transfer none): The IMSI of the #MMSim object, or %NULL if it
 * couldn't be retrieved.
 *
 * Since: 1.0
 */
const gchar *
mm_sim_get_imsi (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sim_get_imsi (MM_GDBUS_SIM (self)));
}

/**
 * mm_sim_dup_imsi:
 * @self: A #MMSim.
 *
 * Gets a copy of the International Mobile Subscriber Identity (IMSI) of the
 * #MMSim object.
 *
 * Returns: (transfer full): The IMSI of the #MMSim object, or %NULL if it
 * couldn't be retrieved. The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_sim_dup_imsi (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sim_dup_imsi (MM_GDBUS_SIM (self)));
}

/*****************************************************************************/

/**
 * mm_sim_get_eid:
 * @self: A #MMSim.
 *
 * Gets the Embedded UICC ID (or EID) of the #MMSim object.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_sim_dup_eid() if on another thread.</warning>
 *
 * Returns: (transfer none): The EID of the #MMSim object, or %NULL if it
 * couldn't be retrieved.
 *
 * Since: 1.16
 */
const gchar *
mm_sim_get_eid (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sim_get_eid (MM_GDBUS_SIM (self)));
}

/**
 * mm_sim_dup_eid:
 * @self: A #MMSim.
 *
 * Gets a copy of the Embedded UICC ID (EID) of the #MMSim object.
 *
 * Returns: (transfer full): The EID of the #MMSim object, or %NULL if it
 * couldn't be retrieved. The returned value should be freed with g_free().
 *
 * Since: 1.16
 */
gchar *
mm_sim_dup_eid (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sim_dup_eid (MM_GDBUS_SIM (self)));
}

/*****************************************************************************/

/**
 * mm_sim_get_operator_identifier:
 * @self: A #MMSim.
 *
 * Gets the Operator Identifier of the #MMSim object.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_sim_dup_operator_identifier() if on another thread.</warning>
 *
 * Returns: (transfer none): The Operator Identifier of the #MMSim object, or
 * %NULL if it couldn't be retrieved.
 *
 * Since: 1.0
 */
const gchar *
mm_sim_get_operator_identifier (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sim_get_operator_identifier (MM_GDBUS_SIM (self)));
}

/**
 * mm_sim_dup_operator_identifier:
 * @self: A #MMSim.
 *
 * Gets a copy of the Operator Identifier of the #MMSim object.
 *
 * Returns: (transfer full): The Operator Identifier of the #MMSim object, or
 * %NULL if it couldn't be retrieved. The returned value should be freed with
 * g_free().
 *
 * Since: 1.0
 */
gchar *
mm_sim_dup_operator_identifier (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sim_dup_operator_identifier (MM_GDBUS_SIM (self)));
}

/*****************************************************************************/

/**
 * mm_sim_get_operator_name:
 * @self: A #MMSim.
 *
 * Gets the Operator Name of the #MMSim object.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_sim_dup_operator_name() if on another thread.</warning>
 *
 * Returns: (transfer none): The Operator Name of the #MMSim object, or %NULL if
 * it couldn't be retrieved.
 *
 * Since: 1.0
 */
const gchar *
mm_sim_get_operator_name (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_sim_get_operator_name (MM_GDBUS_SIM (self)));
}

/**
 * mm_sim_dup_operator_name:
 * @self: A #MMSim.
 *
 * Gets a copy of the Operator Name of the #MMSim object.
 *
 * Returns: (transfer full): The Operator Name of the #MMSim object, or %NULL if
 * it couldn't be retrieved. The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_sim_dup_operator_name (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_sim_dup_operator_name (MM_GDBUS_SIM (self)));
}

/*****************************************************************************/

/**
 * mm_sim_get_emergency_numbers:
 * @self: A #MMSim.
 *
 * Gets the list of emergency call numbers programmed in the SIM card.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_sim_dup_emergency_numbers() if on another thread.</warning>
 *
 * Returns: (transfer none): The emergency numbers, or %NULL if none available.
 * Do not free the returned value, it belongs to @self.
 *
 * Since: 1.12
 */
const gchar * const *
mm_sim_get_emergency_numbers (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    return mm_gdbus_sim_get_emergency_numbers (MM_GDBUS_SIM (self));
}

/**
 * mm_sim_dup_emergency_numbers:
 * @self: A #MMSim.
 *
 * Gets a copy of the list of emergency call numbers programmed in the SIM card.
 *
 * Returns: (transfer full): The emergency numbers, or %NULL if none available.
 * The returned value should be freed with g_strfreev().
 *
 * Since: 1.12
 */
gchar **
mm_sim_dup_emergency_numbers (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    return mm_gdbus_sim_dup_emergency_numbers (MM_GDBUS_SIM (self));
}

/*****************************************************************************/

/**
 * mm_sim_get_preferred_networks:
 * @self: A #MMSim.
 *
 * Gets the list of #MMSimPreferredNetwork objects exposed by this
 * #MMSim.
 *
 * Returns: (transfer full) (element-type ModemManager.SimPreferredNetwork): a list of
 * #MMSimPreferredNetwork objects, or #NULL. The returned value should
 * be freed with g_list_free_full() using mm_sim_preferred_network_free() as #GDestroyNotify
 * function.
 *
 * Since: 1.18
 */
GList *
mm_sim_get_preferred_networks (MMSim *self)
{
    GList *network_list = NULL;
    GVariant *container;

    g_return_val_if_fail (MM_IS_SIM (self), NULL);

    container = mm_gdbus_sim_get_preferred_networks (MM_GDBUS_SIM (self));
    network_list = mm_sim_preferred_network_list_new_from_variant (container);

    return network_list;
}

/*****************************************************************************/

/**
 * mm_sim_get_gid1:
 * @self: A #MMSim.
 * @data_len: (out): Size of the output data, if any given.
 *
 * Gets the Group Identifier Level 1 of the #MMSim object.
 *
 * Returns: (transfer none) (array length=data_len) (element-type guint8): The
 * GID1 data, or %NULL if unknown.
 *
 * Since: 1.20
 */
const guint8 *
mm_sim_get_gid1 (MMSim *self,
                 gsize *data_len)
{
    GVariant *value;

    g_return_val_if_fail (MM_IS_SIM (self), NULL);
    g_return_val_if_fail (data_len != NULL, NULL);

    value = mm_gdbus_sim_get_gid1 (MM_GDBUS_SIM (self));
    return (value ?
            g_variant_get_fixed_array (value, data_len, sizeof (guint8)) :
            NULL);
}

/**
 * mm_sim_dup_gid1:
 * @self: A #MMSim.
 * @data_len: (out): Size of the output data, if any given.
 *
 * Gets the Group Identifier Level 1 of the #MMSim object.
 *
 * Returns: (transfer full) (array length=data_len) (element-type guint8): The
 * GID1 data, or %NULL if unknown.
 *
 * Since: 1.20
 */
guint8 *
mm_sim_dup_gid1 (MMSim *self,
                 gsize *data_len)
{
    g_autoptr(GVariant) value = NULL;

    g_return_val_if_fail (MM_IS_SIM (self), NULL);
    g_return_val_if_fail (data_len != NULL, NULL);

    value = mm_gdbus_sim_dup_gid1 (MM_GDBUS_SIM (self));
    return (value ?
            g_memdup (g_variant_get_fixed_array (value, data_len, sizeof (guint8)), *data_len) :
            NULL);
}

/*****************************************************************************/

/**
 * mm_sim_get_gid2:
 * @self: A #MMSim.
 * @data_len: (out): Size of the output data, if any given.
 *
 * Gets the Group Identifier Level 2 of the #MMSim object.
 *
 * Returns: (transfer none) (array length=data_len) (element-type guint8): The
 * GID2 data, or %NULL if unknown.
 *
 * Since: 1.20
 */
const guint8 *
mm_sim_get_gid2 (MMSim *self,
                 gsize *data_len)
{
    GVariant *value;

    g_return_val_if_fail (MM_IS_SIM (self), NULL);
    g_return_val_if_fail (data_len != NULL, NULL);

    value = mm_gdbus_sim_get_gid2 (MM_GDBUS_SIM (self));
    return (value ?
            g_variant_get_fixed_array (value, data_len, sizeof (guint8)) :
            NULL);
}

/**
 * mm_sim_dup_gid2:
 * @self: A #MMSim.
 * @data_len: (out): Size of the output data, if any given.
 *
 * Gets the Group Identifier Level 2 of the #MMSim object.
 *
 * Returns: (transfer full) (array length=data_len) (element-type guint8): The
 * GID2 data, or %NULL if unknown.
 *
 * Since: 1.20
 */
guint8 *
mm_sim_dup_gid2 (MMSim *self,
                 gsize *data_len)
{
    g_autoptr(GVariant) value = NULL;

    g_return_val_if_fail (MM_IS_SIM (self), NULL);
    g_return_val_if_fail (data_len != NULL, NULL);

    value = mm_gdbus_sim_dup_gid2 (MM_GDBUS_SIM (self));
    return (value ?
            g_memdup (g_variant_get_fixed_array (value, data_len, sizeof (guint8)), *data_len) :
            NULL);
}

/*****************************************************************************/

/**
 * mm_sim_get_sim_type:
 * @self: A #MMSim.
 *
 * Gets the SIM type.
 *
 * Returns: a #MMSimType.
 *
 * Since: 1.20
 */
MMSimType
mm_sim_get_sim_type (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), MM_SIM_TYPE_UNKNOWN);

    return mm_gdbus_sim_get_sim_type (MM_GDBUS_SIM (self));
}

/*****************************************************************************/

/**
 * mm_sim_get_esim_status:
 * @self: A #MMSim.
 *
 * Gets the eSIM status.
 *
 * Only applicable if the SIM type is %MM_SIM_TYPE_ESIM.
 *
 * Returns: a #MMSimEsimStatus.
 *
 * Since: 1.20
 */
MMSimEsimStatus
mm_sim_get_esim_status (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), MM_SIM_ESIM_STATUS_UNKNOWN);

    return mm_gdbus_sim_get_esim_status (MM_GDBUS_SIM (self));
}

/*****************************************************************************/

/**
 * mm_sim_get_removability:
 * @self: A #MMSim.
 *
 * Gets whether the SIM is removable or not.
 *
 * Returns: a #MMSimRemovability.
 *
 * Since: 1.20
 */
MMSimRemovability
mm_sim_get_removability (MMSim *self)
{
    g_return_val_if_fail (MM_IS_SIM (self), MM_SIM_REMOVABILITY_UNKNOWN);

    return mm_gdbus_sim_get_removability (MM_GDBUS_SIM (self));
}

/*****************************************************************************/

/**
 * mm_sim_send_pin_finish:
 * @self: A #MMSim.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_sim_send_pin().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_sim_send_pin().
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_sim_send_pin_finish (MMSim *self,
                        GAsyncResult *res,
                        GError **error)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return mm_gdbus_sim_call_send_pin_finish (MM_GDBUS_SIM (self), res, error);
}

/**
 * mm_sim_send_pin:
 * @self: A #MMSim.
 * @pin: The PIN code.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously sends the PIN code to the SIM card.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_sim_send_pin_finish() to get the result of the operation.
 *
 * See mm_sim_send_pin_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
 */
void
mm_sim_send_pin (MMSim *self,
                 const gchar *pin,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    g_return_if_fail (MM_IS_SIM (self));

    mm_gdbus_sim_call_send_pin (MM_GDBUS_SIM (self),
                                pin,
                                cancellable,
                                callback,
                                user_data);
}

/**
 * mm_sim_send_pin_sync:
 * @self: A #MMSim.
 * @pin: The PIN code.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously sends the PIN to the SIM card.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_sim_send_pin() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_sim_send_pin_sync (MMSim *self,
                      const gchar *pin,
                      GCancellable *cancellable,
                      GError **error)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return (mm_gdbus_sim_call_send_pin_sync (MM_GDBUS_SIM (self),
                                             pin,
                                             cancellable,
                                             error));
}

/*****************************************************************************/

/**
 * mm_sim_send_puk_finish:
 * @self: A #MMSim.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_sim_send_puk().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_sim_send_puk().
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_sim_send_puk_finish (MMSim *self,
                        GAsyncResult *res,
                        GError **error)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return mm_gdbus_sim_call_send_puk_finish (MM_GDBUS_SIM (self), res, error);
}

/**
 * mm_sim_send_puk:
 * @self: A #MMSim.
 * @puk: The PUK code.
 * @pin: The PIN code.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously sends the PUK code to the SIM card.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_sim_send_puk_finish() to get the result of the operation.
 *
 * See mm_sim_send_puk_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
 */
void
mm_sim_send_puk (MMSim *self,
                 const gchar *puk,
                 const gchar *pin,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    g_return_if_fail (MM_IS_SIM (self));

    mm_gdbus_sim_call_send_puk (MM_GDBUS_SIM (self),
                                puk,
                                pin,
                                cancellable,
                                callback,
                                user_data);
}

/**
 * mm_sim_send_puk_sync:
 * @self: A #MMSim.
 * @puk: The PUK code.
 * @pin: The PIN code.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously sends the PUK to the SIM card.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_sim_send_puk() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_sim_send_puk_sync (MMSim *self,
                      const gchar *puk,
                      const gchar *pin,
                      GCancellable *cancellable,
                      GError **error)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return (mm_gdbus_sim_call_send_puk_sync (MM_GDBUS_SIM (self),
                                             puk,
                                             pin,
                                             cancellable,
                                             error));
}

/*****************************************************************************/

/**
 * mm_sim_enable_pin_finish:
 * @self: A #MMSim.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_sim_enable_pin().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_sim_enable_pin().
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_sim_enable_pin_finish (MMSim *self,
                          GAsyncResult *res,
                          GError **error)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return mm_gdbus_sim_call_enable_pin_finish (MM_GDBUS_SIM (self), res, error);
}

/**
 * mm_sim_enable_pin:
 * @self: A #MMSim.
 * @pin: The PIN code.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously enables requesting the PIN code in the SIM card.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_sim_enable_pin_finish() to get the result of the operation.
 *
 * See mm_sim_enable_pin_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
 */
void
mm_sim_enable_pin (MMSim *self,
                   const gchar *pin,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    g_return_if_fail (MM_IS_SIM (self));

    mm_gdbus_sim_call_enable_pin (MM_GDBUS_SIM (self),
                                  pin,
                                  TRUE,
                                  cancellable,
                                  callback,
                                  user_data);
}

/**
 * mm_sim_enable_pin_sync:
 * @self: A #MMSim.
 * @pin: The PIN code.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously enables requesting the PIN code in the SIM card.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_sim_enable_pin() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_sim_enable_pin_sync (MMSim *self,
                        const gchar *pin,
                        GCancellable *cancellable,
                        GError **error)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return (mm_gdbus_sim_call_enable_pin_sync (MM_GDBUS_SIM (self),
                                               pin,
                                               TRUE,
                                               cancellable,
                                               error));
}

/*****************************************************************************/

/**
 * mm_sim_disable_pin_finish:
 * @self: A #MMSim.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_sim_disable_pin().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_sim_disable_pin().
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_sim_disable_pin_finish (MMSim *self,
                           GAsyncResult *res,
                           GError **error)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return mm_gdbus_sim_call_enable_pin_finish (MM_GDBUS_SIM (self), res, error);
}

/**
 * mm_sim_disable_pin:
 * @self: A #MMSim.
 * @pin: The PIN code.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously disables requesting the PIN code in the SIM card.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_sim_disable_pin_finish() to get the result of the operation.
 *
 * See mm_sim_disable_pin_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
 */
void
mm_sim_disable_pin (MMSim *self,
                    const gchar *pin,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    g_return_if_fail (MM_IS_SIM (self));

    mm_gdbus_sim_call_enable_pin (MM_GDBUS_SIM (self),
                                  pin,
                                  FALSE,
                                  cancellable,
                                  callback,
                                  user_data);
}

/**
 * mm_sim_disable_pin_sync:
 * @self: A #MMSim.
 * @pin: The PIN code.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously disables requesting the PIN code in the SIM card.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_sim_disable_pin() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_sim_disable_pin_sync (MMSim *self,
                         const gchar *pin,
                         GCancellable *cancellable,
                         GError **error)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return (mm_gdbus_sim_call_enable_pin_sync (MM_GDBUS_SIM (self),
                                               pin,
                                               FALSE,
                                               cancellable,
                                               error));
}

/*****************************************************************************/

/**
 * mm_sim_change_pin_finish:
 * @self: A #MMSim.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_sim_change_pin().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_sim_change_pin().
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_sim_change_pin_finish (MMSim *self,
                          GAsyncResult *res,
                          GError **error)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return mm_gdbus_sim_call_change_pin_finish (MM_GDBUS_SIM (self), res, error);
}

/**
 * mm_sim_change_pin:
 * @self: A #MMSim.
 * @old_pin: The current PIN code.
 * @new_pin: The new PIN code to be set.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously changes the PIN code in the SIM card.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_sim_change_pin_finish() to get the result of the operation.
 *
 * See mm_sim_change_pin_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
 */
void
mm_sim_change_pin (MMSim *self,
                   const gchar *old_pin,
                   const gchar *new_pin,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    g_return_if_fail (MM_IS_SIM (self));

    mm_gdbus_sim_call_change_pin (MM_GDBUS_SIM (self),
                                  old_pin,
                                  new_pin,
                                  cancellable,
                                  callback,
                                  user_data);
}

/**
 * mm_sim_change_pin_sync:
 * @self: A #MMSim.
 * @old_pin: The current PIN code.
 * @new_pin: The new PIN code to be set.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously changes the PIN code in the SIM card.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_sim_change_pin() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_sim_change_pin_sync (MMSim *self,
                        const gchar *old_pin,
                        const gchar *new_pin,
                        GCancellable *cancellable,
                        GError **error)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return (mm_gdbus_sim_call_change_pin_sync (MM_GDBUS_SIM (self),
                                               old_pin,
                                               new_pin,
                                               cancellable,
                                               error));
}

/*****************************************************************************/

/**
 * mm_sim_set_preferred_networks_finish:
 * @self: A #MMSim.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_sim_set_preferred_networks().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_sim_set_preferred_networks().
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.18
 */
gboolean
mm_sim_set_preferred_networks_finish (MMSim *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    return mm_gdbus_sim_call_set_preferred_networks_finish (MM_GDBUS_SIM (self), res, error);
}

/**
 * mm_sim_set_preferred_networks:
 * @self: A #MMSim.
 * @preferred_networks: (element-type ModemManager.SimPreferredNetwork):
 *  A list of #MMSimPreferredNetwork objects
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously sets the preferred network list of this #MMSim.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_sim_set_preferred_networks_finish() to get the result of
 * the operation.
 *
 * Since: 1.18
 */
void
mm_sim_set_preferred_networks (MMSim *self,
                               const GList *preferred_networks,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GVariant *networks_list;

    g_return_if_fail (MM_IS_SIM (self));

    networks_list = mm_sim_preferred_network_list_get_variant (preferred_networks);

    mm_gdbus_sim_call_set_preferred_networks (MM_GDBUS_SIM (self),
                                              networks_list,
                                              cancellable,
                                              callback,
                                              user_data);
}

/**
 * mm_sim_set_preferred_networks_sync:
 * @self: A #MMSim.
 * @preferred_networks: (element-type ModemManager.SimPreferredNetwork):
 *  A list of #MMSimPreferredNetwork objects
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously sets the preferred network list of this #MMSim.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_sim_set_preferred_networks() for the asynchronous
 * version of this method.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_sim_set_preferred_networks_finish() to get the result of
 * the operation.
 *
 * Since: 1.18
 */
gboolean
mm_sim_set_preferred_networks_sync (MMSim *self,
                                    const GList *preferred_networks,
                                    GCancellable *cancellable,
                                    GError **error)
{
    gboolean  result;
    GVariant *networks_list;

    g_return_val_if_fail (MM_IS_SIM (self), FALSE);

    networks_list = mm_sim_preferred_network_list_get_variant (preferred_networks);

    result = mm_gdbus_sim_call_set_preferred_networks_sync (MM_GDBUS_SIM (self),
                                                            networks_list,
                                                            cancellable,
                                                            error);
    return result;
}

/*****************************************************************************/

static void
mm_sim_init (MMSim *self)
{
}

static void
mm_sim_class_init (MMSimClass *sim_class)
{
}
