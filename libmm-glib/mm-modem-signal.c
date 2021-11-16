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
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2012 Lanedo GmbH <aleksander@lanedo.com>
 * Copyright (C) 2013-2021 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Intel Corporation
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-errors-types.h"
#include "mm-modem-signal.h"

/**
 * SECTION: mm-modem-signal
 * @title: MMModemSignal
 * @short_description: The extended Signal interface
 *
 * The #MMModemSignal is an object providing access to the methods, signals and
 * properties of the Signal interface.
 *
 * The Signal interface is exposed whenever a modem has extended signal
 * retrieval capabilities.
 */

G_DEFINE_TYPE (MMModemSignal, mm_modem_signal, MM_GDBUS_TYPE_MODEM_SIGNAL_PROXY)

struct _MMModemSignalPrivate {
    /* Common mutex to sync access */
    GMutex mutex;

    PROPERTY_OBJECT_DECLARE (cdma, MMSignal)
    PROPERTY_OBJECT_DECLARE (evdo, MMSignal)
    PROPERTY_OBJECT_DECLARE (gsm,  MMSignal)
    PROPERTY_OBJECT_DECLARE (umts, MMSignal)
    PROPERTY_OBJECT_DECLARE (lte,  MMSignal)
    PROPERTY_OBJECT_DECLARE (nr5g, MMSignal)
};

/*****************************************************************************/

/**
 * mm_modem_signal_get_path:
 * @self: A #MMModemSignal.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.2
 */
const gchar *
mm_modem_signal_get_path (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_signal_dup_path:
 * @self: A #MMModemSignal.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this
 * interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value
 * should be freed with g_free().
 *
 * Since: 1.2
 */
gchar *
mm_modem_signal_dup_path (MMModemSignal *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_signal_setup_finish:
 * @self: A #MMModemSignal.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_signal_setup().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_signal_setup().
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_signal_setup_finish (MMModemSignal *self,
                              GAsyncResult  *res,
                              GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), FALSE);

    return mm_gdbus_modem_signal_call_setup_finish (MM_GDBUS_MODEM_SIGNAL (self), res, error);
}

/**
 * mm_modem_signal_setup:
 * @self: A #MMModemSignal.
 * @rate: Refresh rate to set, in seconds. Use 0 to disable periodic polling.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously enables or disables the extended signal quality information
 * retrieval via periodic polling.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_signal_setup_finish() to get the result of the operation.
 *
 * See mm_modem_signal_setup_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.2
 */
void
mm_modem_signal_setup (MMModemSignal       *self,
                       guint                rate,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
    g_return_if_fail (MM_IS_MODEM_SIGNAL (self));

    mm_gdbus_modem_signal_call_setup (MM_GDBUS_MODEM_SIGNAL (self), rate, cancellable, callback, user_data);
}

/**
 * mm_modem_signal_setup_sync:
 * @self: A #MMModemSignal.
 * @rate: Refresh rate to set, in seconds. Use 0 to disable periodic polling.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously enables or disables the extended signal quality information
 * retrieval via periodic polling.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_signal_setup() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_signal_setup_sync (MMModemSignal  *self,
                            guint           rate,
                            GCancellable   *cancellable,
                            GError        **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), FALSE);

    return mm_gdbus_modem_signal_call_setup_sync (MM_GDBUS_MODEM_SIGNAL (self), rate, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_signal_setup_thresholds_finish:
 * @self: A #MMModemSignal.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_signal_setup_thresholds().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_signal_setup_thresholds().
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 *
 * Since: 1.20
 */
gboolean
mm_modem_signal_setup_thresholds_finish (MMModemSignal  *self,
                                         GAsyncResult   *res,
                                         GError        **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), FALSE);

    return mm_gdbus_modem_signal_call_setup_thresholds_finish (MM_GDBUS_MODEM_SIGNAL (self), res, error);
}

/**
 * mm_modem_signal_setup_thresholds:
 * @self: A #MMModemSignal.
 * @properties: Threshold values to set.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously setups thresholds so that the device itself decides when to report the
 * extended signal quality information updates.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_signal_setup_thresholds_finish() to get the result of the operation.
 *
 * See mm_modem_signal_setup_thresholds_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.20
 */
void
mm_modem_signal_setup_thresholds (MMModemSignal               *self,
                                  MMSignalThresholdProperties *properties,
                                  GCancellable                *cancellable,
                                  GAsyncReadyCallback          callback,
                                  gpointer                     user_data)
{
    g_autoptr(GVariant) dictionary = NULL;

    g_return_if_fail (MM_IS_MODEM_SIGNAL (self));

    dictionary = mm_signal_threshold_properties_get_dictionary (properties);
    mm_gdbus_modem_signal_call_setup_thresholds (MM_GDBUS_MODEM_SIGNAL (self), dictionary, cancellable, callback, user_data);
}

/**
 * mm_modem_signal_setup_thresholds_sync:
 * @self: A #MMModemSignal.
 * @properties: Threshold values to set.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously setups thresholds so that the device itself decides when to report the
 * extended signal quality information updates.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_signal_setup_thresholds() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 *
 * Since: 1.20
 */
gboolean
mm_modem_signal_setup_thresholds_sync (MMModemSignal                *self,
                                       MMSignalThresholdProperties  *properties,
                                       GCancellable                 *cancellable,
                                       GError                      **error)
{
    g_autoptr(GVariant) dictionary = NULL;

    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), FALSE);

    dictionary = mm_signal_threshold_properties_get_dictionary (properties);
    return mm_gdbus_modem_signal_call_setup_thresholds_sync (MM_GDBUS_MODEM_SIGNAL (self), dictionary, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_signal_get_rate:
 * @self: A #MMModemSignal.
 *
 * Gets the currently configured refresh rate.
 *
 * Returns: the refresh rate, in seconds.
 *
 * Since: 1.2
 */
guint
mm_modem_signal_get_rate (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), 0);

    return mm_gdbus_modem_signal_get_rate (MM_GDBUS_MODEM_SIGNAL (self));
}

/*****************************************************************************/

/**
 * mm_modem_signal_get_rssi_threshold:
 * @self: A #MMModemSignal.
 *
 * Gets the currently configured RSSI threshold, in dBm.
 *
 * A value of 0 indicates the threshold is disabled.
 *
 * Returns: the RSSI threshold.
 *
 * Since: 1.20
 */
guint
mm_modem_signal_get_rssi_threshold (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), 0);

    return mm_gdbus_modem_signal_get_rssi_threshold (MM_GDBUS_MODEM_SIGNAL (self));
}

/*****************************************************************************/

/**
 * mm_modem_signal_get_error_rate_threshold:
 * @self: A #MMModemSignal.
 *
 * Gets whether the error rate threshold is enabled or not.
 *
 * Returns: %TRUE if the error rate threshold is enabled, %FALSE otherwise.
 *
 * Since: 1.20
 */
gboolean
mm_modem_signal_get_error_rate_threshold (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), FALSE);

    return mm_gdbus_modem_signal_get_error_rate_threshold (MM_GDBUS_MODEM_SIGNAL (self));
}

/*****************************************************************************/

/**
 * mm_modem_signal_get_cdma:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the CDMA signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_cdma() again to get a new #MMSignal with the new values.
 * </warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.2
 */

/**
 * mm_modem_signal_peek_cdma:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the CDMA signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_cdma() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.2
 */

PROPERTY_OBJECT_DEFINE_FAILABLE (cdma,
                                 ModemSignal, modem_signal, MODEM_SIGNAL,
                                 MMSignal,
                                 mm_signal_new_from_dictionary)

/*****************************************************************************/

/**
 * mm_modem_signal_get_evdo:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the EV-DO signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_evdo() again to get a new #MMSignal with the new values.
 * </warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.2
 */

/**
 * mm_modem_signal_peek_evdo:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the EV-DO signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_evdo() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.2
 */

PROPERTY_OBJECT_DEFINE_FAILABLE (evdo,
                                 ModemSignal, modem_signal, MODEM_SIGNAL,
                                 MMSignal,
                                 mm_signal_new_from_dictionary)

/*****************************************************************************/

/**
 * mm_modem_signal_get_gsm:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the GSM signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_gsm() again to get a new #MMSignal with the
 * new values.</warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.2
 */

/**
 * mm_modem_signal_peek_gsm:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the GSM signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_gsm() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.2
 */

PROPERTY_OBJECT_DEFINE_FAILABLE (gsm,
                                 ModemSignal, modem_signal, MODEM_SIGNAL,
                                 MMSignal,
                                 mm_signal_new_from_dictionary)

/*****************************************************************************/

/**
 * mm_modem_signal_get_umts:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the UMTS signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_umts() again to get a new #MMSignal with the new values.
 * </warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.2
 */

/**
 * mm_modem_signal_peek_umts:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the UMTS signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_umts() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.2
 */

PROPERTY_OBJECT_DEFINE_FAILABLE (umts,
                                 ModemSignal, modem_signal, MODEM_SIGNAL,
                                 MMSignal,
                                 mm_signal_new_from_dictionary)

/*****************************************************************************/

/**
 * mm_modem_signal_get_lte:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the LTE signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_lte() again to get a new #MMSignal with the new values.
 * </warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.2
 */

/**
 * mm_modem_signal_peek_lte:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the LTE signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_lte() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.2
 */

PROPERTY_OBJECT_DEFINE_FAILABLE (lte,
                                 ModemSignal, modem_signal, MODEM_SIGNAL,
                                 MMSignal,
                                 mm_signal_new_from_dictionary)

/*****************************************************************************/

/**
 * mm_modem_signal_get_nr5g:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the 5G signal information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_signal_get_nr5g() again to get a new #MMSignal with the new values.
 * </warning>
 *
 * Returns: (transfer full): A #MMSignal that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.16
 */

/**
 * mm_modem_signal_peek_nr5g:
 * @self: A #MMModem.
 *
 * Gets a #MMSignal object specifying the 5G signal information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_signal_get_nr5g() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMSignal. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.16
 */

PROPERTY_OBJECT_DEFINE_FAILABLE (nr5g,
                                 ModemSignal, modem_signal, MODEM_SIGNAL,
                                 MMSignal,
                                 mm_signal_new_from_dictionary)

/*****************************************************************************/

static void
mm_modem_signal_init (MMModemSignal *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_MODEM_SIGNAL, MMModemSignalPrivate);
    g_mutex_init (&self->priv->mutex);

    PROPERTY_INITIALIZE (cdma, "cdma")
    PROPERTY_INITIALIZE (evdo, "evdo")
    PROPERTY_INITIALIZE (gsm,  "gsm")
    PROPERTY_INITIALIZE (umts, "umts")
    PROPERTY_INITIALIZE (lte,  "lte")
    PROPERTY_INITIALIZE (nr5g, "nr5g")
}

static void
finalize (GObject *object)
{
    MMModemSignal *self = MM_MODEM_SIGNAL (object);

    g_mutex_clear (&self->priv->mutex);

    PROPERTY_OBJECT_FINALIZE (cdma)
    PROPERTY_OBJECT_FINALIZE (evdo)
    PROPERTY_OBJECT_FINALIZE (gsm)
    PROPERTY_OBJECT_FINALIZE (umts)
    PROPERTY_OBJECT_FINALIZE (lte)
    PROPERTY_OBJECT_FINALIZE (nr5g)

    G_OBJECT_CLASS (mm_modem_signal_parent_class)->finalize (object);
}

static void
mm_modem_signal_class_init (MMModemSignalClass *modem_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (modem_class);

    g_type_class_add_private (object_class, sizeof (MMModemSignalPrivate));

    object_class->finalize = finalize;
}
