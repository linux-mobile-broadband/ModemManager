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
 * Copyright (C) 2021 Fibocom Wireless Inc.
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-errors-types.h"
#include "mm-modem-sar.h"
#include "mm-common-helpers.h"

/**
 * SECTION: mm-modem-sar
 * @title: MMModemSar
 * @short_description: The SAR interface
 *
 * The #MMModemSar is an object providing access to the methods, signals and
 * properties of the SAR interface.
 *
 * The SAR interface is exposed whenever a modem has SAR capabilities.
 */

G_DEFINE_TYPE (MMModemSar, mm_modem_sar, MM_GDBUS_TYPE_MODEM_SAR_PROXY)

/*****************************************************************************/

/**
 * mm_modem_sar_get_path:
 * @self: A #MMModemSar.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.20
 */
const gchar *
mm_modem_sar_get_path (MMModemSar *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SAR (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_sar_dup_path:
 * @self: A #MMModemSar.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this
 * interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value
 * should be freed with g_free().
 *
 * Since: 1.20
 */
gchar *
mm_modem_sar_dup_path (MMModemSar *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_SAR (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}
/*****************************************************************************/

/**
 * mm_modem_sar_enable_finish:
 * @self: A #MMModemSar.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_sar_enable().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_sar_enable().
 *
 * Returns: %TRUE if the enable was successful, %FALSE if @error is set.
 *
 * Since: 1.20
 */
gboolean
mm_modem_sar_enable_finish (MMModemSar   *self,
                            GAsyncResult *res,
                            GError      **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SAR (self), FALSE);

    return mm_gdbus_modem_sar_call_enable_finish (MM_GDBUS_MODEM_SAR (self), res, error);
}

/**
 * mm_modem_sar_enable:
 * @self: A #MMModemSar.
 * @enable: %TRUE to enable dynamic SAR and %FALSE to disable it.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously enable or disable dynamic SAR.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_sar_enable_finish() to get the result of the operation.
 *
 * See mm_modem_sar_enable_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.20
 */
void
mm_modem_sar_enable (MMModemSar          *self,
                     gboolean             enable,
                     GCancellable        *cancellable,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    g_return_if_fail (MM_IS_MODEM_SAR (self));

    mm_gdbus_modem_sar_call_enable (MM_GDBUS_MODEM_SAR (self), enable, cancellable, callback, user_data);
}

/**
 * mm_modem_sar_enable_sync:
 * @self: A #MMModemSar.
 * @enable: %TRUE to enable dynamic SAR and %FALSE to disable it.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously enable or disable dynamic SAR.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_sar_enable() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the enable was successful, %FALSE if @error is set.
 *
 * Since: 1.20
 */
gboolean
mm_modem_sar_enable_sync (MMModemSar   *self,
                          gboolean      enable,
                          GCancellable *cancellable,
                          GError      **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SAR (self), FALSE);

    return mm_gdbus_modem_sar_call_enable_sync (MM_GDBUS_MODEM_SAR (self), enable, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_sar_power_level_finish:
 * @self: A #MMModemSar.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_sar_enable().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_sar_set_power_level().
 *
 * Returns: %TRUE if set power level was successful, %FALSE if @error is set.
 *
 * Since: 1.20
 */
gboolean
mm_modem_sar_set_power_level_finish (MMModemSar   *self,
                                     GAsyncResult *res,
                                     GError      **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SAR (self), FALSE);

    return mm_gdbus_modem_sar_call_set_power_level_finish (MM_GDBUS_MODEM_SAR (self), res, error);
}

/**
 * mm_modem_sar_set_power_level:
 * @self: A #MMModemSar.
 * @level: Index of the SAR power level mapping table
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously set current dynamic SAR power level.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_sar_set_power_level_finish() to get the result of the operation.
 *
 * See mm_modem_sar_set_power_level_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.20
 */
void
mm_modem_sar_set_power_level (MMModemSar         *self,
                              guint               level,
                              GCancellable       *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer            user_data)
{
    g_return_if_fail (MM_IS_MODEM_SAR (self));

    mm_gdbus_modem_sar_call_set_power_level (MM_GDBUS_MODEM_SAR (self), level, cancellable, callback, user_data);
}

/**
 * mm_modem_sar_set_power_level_sync:
 * @self: A #MMModemSar.
 * @level: Index of the SAR power level mapping table
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously set current dynamic SAR power level.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_sar_set_power_level() for the asynchronous version of this method.
 *
 * Returns: %TRUE if set power level was successful, %FALSE if @error is set.
 *
 * Since: 1.20
 */
gboolean
mm_modem_sar_set_power_level_sync (MMModemSar   *self,
                                   guint         level,
                                   GCancellable *cancellable,
                                   GError      **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SAR (self), FALSE);

    return mm_gdbus_modem_sar_call_set_power_level_sync (MM_GDBUS_MODEM_SAR (self), level, cancellable, error);
}

/*****************************************************************************/
/**
 * mm_modem_sar_get_state:
 * @self: A #MMModem.
 *
 * Gets the state of dynamic SAR.
 *
 * Returns: %TRUE if dynamic SAR is enabled, %FALSE otherwise.
 *
 * Since: 1.20
 */
gboolean
mm_modem_sar_get_state (MMModemSar *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SAR (self), FALSE);

    return mm_gdbus_modem_sar_get_state (MM_GDBUS_MODEM_SAR (self));
}

/*****************************************************************************/
/**
 * mm_modem_sar_get_power_level:
 * @self: A #MMModem.
 *
 * Gets the index of the SAR power level mapping table.
 *
 * Returns: the index.
 *
 * Since: 1.20
 */
guint
mm_modem_sar_get_power_level (MMModemSar *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SAR (self), FALSE);

    return mm_gdbus_modem_sar_get_power_level (MM_GDBUS_MODEM_SAR (self));
}

/*****************************************************************************/
static void
mm_modem_sar_init (MMModemSar *self)
{
}

static void
mm_modem_sar_class_init (MMModemSarClass *modem_class)
{
    /* Virtual methods */
}
