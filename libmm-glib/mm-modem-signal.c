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
 * Copyright (C) 2012 Lanedo GmbH <aleksander@lanedo.com>
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
 * The Signal interface is exposed whenever a modem has extended signal retrieval
 * capabilities.
 */

G_DEFINE_TYPE (MMModemSignal, mm_modem_signal, MM_GDBUS_TYPE_MODEM_SIGNAL_PROXY)

/*****************************************************************************/

/**
 * mm_modem_signal_get_path:
 * @self: A #MMModemSignal.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
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
 * Gets a copy of the DBus path of the #MMObject object which implements this interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
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
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_signal_setup().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_signal_setup().
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 */
gboolean
mm_modem_signal_setup_finish (MMModemSignal *self,
                  GAsyncResult *res,
                  GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), FALSE);

    return mm_gdbus_modem_signal_call_setup_finish (MM_GDBUS_MODEM_SIGNAL (self), res, error);
}

/**
 * mm_modem_signal_setup:
 * @self: A #MMModemSignal.
 * @rate: Rate to use when refreshing signal values.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously setups the extended signal quality retrieval.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_signal_setup_finish() to get the result of the operation.
 *
 * See mm_modem_signal_setup_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_signal_setup (MMModemSignal *self,
               guint rate,
               GCancellable *cancellable,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_SIGNAL (self));

    mm_gdbus_modem_signal_call_setup (MM_GDBUS_MODEM_SIGNAL (self), rate, cancellable, callback, user_data);
}

/**
 * mm_modem_signal_setup_sync:
 * @self: A #MMModemSignal.
 * @rate: Rate to use when refreshing signal values.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously setups the extended signal quality retrieval.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_signal_setup()
 * for the asynchronous version of this method.
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 */
gboolean
mm_modem_signal_setup_sync (MMModemSignal *self,
                guint rate,
                GCancellable *cancellable,
                GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), FALSE);

    return mm_gdbus_modem_signal_call_setup_sync (MM_GDBUS_MODEM_SIGNAL (self), rate, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_get_rate:
 * @self: A #MMModemSignal.
 *
 * Gets the currently configured refresh rate.
 *
 * Returns: the refresh rate, in seconds.
 */
guint
mm_modem_signal_get_rate (MMModemSignal *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), 0);

    return mm_gdbus_modem_signal_get_rate (MM_GDBUS_MODEM_SIGNAL (self));
}

/*****************************************************************************/

#define GETTER(VALUE)                                                   \
    gboolean                                                            \
    mm_modem_signal_get_##VALUE (MMModemSignal *self,                   \
                                gdouble *value)                         \
    {                                                                   \
        GVariant *variant;                                              \
        gboolean is_valid = FALSE;                                      \
        double val = 0.0;                                               \
                                                                        \
        g_return_val_if_fail (MM_IS_MODEM_SIGNAL (self), FALSE);        \
                                                                        \
        variant = mm_gdbus_modem_signal_dup_##VALUE (MM_GDBUS_MODEM_SIGNAL (self)); \
            if (variant) {                                              \
                g_variant_get (variant,                                 \
                               "(bd)",                                  \
                               &is_valid,                               \
                               &val);                                   \
                g_variant_unref (variant);                              \
            }                                                           \
                                                                        \
            if (is_valid)                                               \
                *value = val;                                           \
            return is_valid;                                            \
    }

/**
 * mm_modem_get_cdma_rssi:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the CDMA1x RSSI (Received Signal Strength Indication), in dBm.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(cdma_rssi)

/**
 * mm_modem_get_cdma_ecio:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the CDMA1x Ec/Io, in dBm.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(cdma_ecio)

/**
 * mm_modem_get_evdo_rssi:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the CDMA EV-DO RSSI (Received Signal Strength Indication), in dBm.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(evdo_rssi)

/**
 * mm_modem_get_evdo_ecio:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the CDMA EV-DO Ec/Io, in dBm.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(evdo_ecio)

/**
 * mm_modem_get_evdo_sinr:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the CDMA EV-DO SINR level, in dB.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(evdo_sinr)

/**
 * mm_modem_get_evdo_io:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the CDMA EV-DO IO, in dBm.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(evdo_io)

/**
 * mm_modem_get_gsm_rssi:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the GSM RSSI (Received Signal Strength Indication), in dBm.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(gsm_rssi)

/**
 * mm_modem_get_umts_rssi:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the UMTS (WCDMA) RSSI (Received Signal Strength Indication), in dBm.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(umts_rssi)

/**
 * mm_modem_get_umts_ecio:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the UMTS (WCDMA) Ec/Io, in dBm.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(umts_ecio)

/**
 * mm_modem_get_lte_rssi:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the LTE RSSI (Received Signal Strength Indication), in dBm.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(lte_rssi)

/**
 * mm_modem_get_lte_rsrq:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the LTE RSRQ (Reference Signal Received Quality), in dB.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(lte_rsrq)

/**
 * mm_modem_get_lte_rsrp:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the LTE RSRP (Reference Signal Received Power), in dBm.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(lte_rsrp)

/**
 * mm_modem_get_lte_snr:
 * @self: A #MMModemSignal.
 * @value: (out): Return location for the value.
 *
 * Gets the LTE S/R ratio, in dB.
 *
 * Returns: %TRUE if @value is valid, %FALSE otherwise.
 */
GETTER(lte_snr)

/*****************************************************************************/

static void
mm_modem_signal_init (MMModemSignal *self)
{
}

static void
mm_modem_signal_class_init (MMModemSignalClass *modem_class)
{
}
