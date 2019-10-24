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

#include "mm-errors-types.h"
#include "mm-object.h"

/**
 * SECTION: mm-object
 * @title: MMObject
 * @short_description: Generic object representing a modem in ModemManager
 *
 * The #MMObject is a generic object which represents any kind of modem exposed
 * in ModemManager, and allows accessing the exported interfaces one by one.
 *
 * When this object is available, it is ensured that at least the Modem
 * interface is also available.
 */

G_DEFINE_TYPE (MMObject, mm_object, MM_GDBUS_TYPE_OBJECT_PROXY)

/*****************************************************************************/

/**
 * mm_object_get_path: (skip)
 * @self: A #MMObject.
 *
 * Gets the DBus path of the #MMObject object.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.0
 */
const gchar *
mm_object_get_path (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (self), NULL);

    return g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
}

/**
 * mm_object_dup_path:
 * @self: A #MMObject.
 *
 * Gets a copy of the DBus path of the #MMObject object.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value
 * should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_object_dup_path (MMObject *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_OBJECT (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    return value;
}

/*****************************************************************************/

/**
 * mm_object_get_modem:
 * @self: A #MMModem
 *
 * Gets the #MMModem instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem on @self, if any.
 *
 * Returns: (transfer full): A #MMModem that must be freed with g_object_unref()
 * or %NULL if @self does not implement the interface.
 *
 * Since: 1.0
 */
MMModem *
mm_object_get_modem (MMObject *self)
{
    MMModem *modem;

    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    modem = (MMModem *)mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self));
    g_warn_if_fail (MM_IS_MODEM (modem));
    return modem;
}

/**
 * mm_object_peek_modem: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem() but doesn't increase the reference count on the
 * returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModem or %NULL if @self does not implement
 * the interface. Do not free the returned object, it is owned by @self.
 *
 * Since: 1.0
 */
MMModem *
mm_object_peek_modem (MMObject *self)
{
    MMModem *modem;

    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    modem = (MMModem *) mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self));
    g_warn_if_fail (MM_IS_MODEM (modem));
    return modem;
}

/*****************************************************************************/

/**
 * mm_object_get_modem_3gpp:
 * @self: A #MMObject.
 *
 * Gets the #MMModem3gpp instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem.Modem3gpp on @self, if any.
 *
 * Returns: (transfer full): A #MMModem3gpp that must be freed with
 * g_object_unref() or %NULL if @self does not implement the interface.
 *
 * Since: 1.0
 */
MMModem3gpp *
mm_object_get_modem_3gpp (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModem3gpp *)mm_gdbus_object_get_modem3gpp (MM_GDBUS_OBJECT (self));
}

/**
 * mm_object_peek_modem_3gpp: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem_3gpp() but doesn't increase the reference count on
 * the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModem3gpp or %NULL if @self does not implement
 * the interface. Do not free the returned object, it is owned by @self.
 *
 * Since: 1.0
 */
MMModem3gpp *
mm_object_peek_modem_3gpp (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModem3gpp *)mm_gdbus_object_peek_modem3gpp (MM_GDBUS_OBJECT (self));
}

/*****************************************************************************/

/**
 * mm_object_get_modem_3gpp_ussd:
 * @self: A #MMObject.
 *
 * Gets the #MMModem3gppUssd instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem.Modem3gpp-Ussd on @self, if any.
 *
 * Returns: (transfer full): A #MMModem3gppUssd that must be freed with
 * g_object_unref() or %NULL if @self does not implement the interface.
 *
 * Since: 1.0
 */
MMModem3gppUssd *
mm_object_get_modem_3gpp_ussd (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModem3gppUssd *)mm_gdbus_object_get_modem3gpp_ussd (MM_GDBUS_OBJECT (self));
}

/**
 * mm_object_peek_modem_3gpp_ussd: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem_3gpp_ussd() but doesn't increase the reference count
 * on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModem3gppUssd or %NULL if @self does not
 * implement the interface. Do not free the returned object, it is owned by
 * @self.
 *
 * Since: 1.0
 */
MMModem3gppUssd *
mm_object_peek_modem_3gpp_ussd (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModem3gppUssd *)mm_gdbus_object_peek_modem3gpp_ussd (MM_GDBUS_OBJECT (self));
}

/*****************************************************************************/

/**
 * mm_object_get_modem_cdma:
 * @self: A #MMObject.
 *
 * Gets the #MMModemCdma instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem.ModemCdma on @self, if any.
 *
 * Returns: (transfer full): A #MMModemCdma that must be freed with
 * g_object_unref() or %NULL if @self does not implement the interface.
 *
 * Since: 1.0
 */
MMModemCdma *
mm_object_get_modem_cdma (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemCdma *)mm_gdbus_object_get_modem_cdma (MM_GDBUS_OBJECT (self));
}

/**
 * mm_object_peek_modem_cdma: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem_cdma() but doesn't increase the reference count on
 * the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModemCdma or %NULL if @self does not implement
 * the interface. Do not free the returned object, it is owned by @self.
 *
 * Since: 1.0
 */
MMModemCdma *
mm_object_peek_modem_cdma (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemCdma *)mm_gdbus_object_peek_modem_cdma (MM_GDBUS_OBJECT (self));
}

/*****************************************************************************/

/**
 * mm_object_get_modem_simple:
 * @self: A #MMObject.
 *
 * Gets the #MMModemSimple instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem.Modemsimple on @self, if any.
 *
 * Returns: (transfer full): A #MMModemSimple that must be freed with
 * g_object_unref() or %NULL if @self does not implement the interface.
 *
 * Since: 1.0
 */
MMModemSimple *
mm_object_get_modem_simple (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemSimple *)mm_gdbus_object_get_modem_simple (MM_GDBUS_OBJECT (self));
}

/**
 * mm_object_peek_modem_simple: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem_simple() but doesn't increase the reference count on
 * the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModemSimple or %NULL if @self does not
 * implement the interface. Do not free the returned object, it is owned by
 * @self.
 *
 * Since: 1.0
 */
MMModemSimple *
mm_object_peek_modem_simple (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemSimple *)mm_gdbus_object_peek_modem_simple (MM_GDBUS_OBJECT (self));
}

/*****************************************************************************/

/**
 * mm_object_get_modem_location:
 * @self: A #MMObject.
 *
 * Gets the #MMModemLocation instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem.Modemlocation on @self, if any.
 *
 * Returns: (transfer full): A #MMModemLocation that must be freed with
 * g_object_unref() or %NULL if @self does not implement the interface.
 *
 * Since: 1.0
 */
MMModemLocation *
mm_object_get_modem_location (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemLocation *)mm_gdbus_object_get_modem_location (MM_GDBUS_OBJECT (self));
}

/**
 * mm_object_peek_modem_location: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem_location() but doesn't increase the reference count
 * on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModemLocation or %NULL if @self does not
 * implement the interface. Do not free the returned object, it is owned by
 * @self.
 *
 * Since: 1.0
 */
MMModemLocation *
mm_object_peek_modem_location (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemLocation *)mm_gdbus_object_peek_modem_location (MM_GDBUS_OBJECT (self));
}

/*****************************************************************************/

/**
 * mm_object_get_modem_messaging:
 * @self: A #MMObject.
 *
 * Gets the #MMModemMessaging instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem.Modemmessaging on @self, if any.
 *
 * Returns: (transfer full): A #MMModemMessaging that must be freed with
 * g_object_unref() or %NULL if @self does not implement the interface.
 *
 * Since: 1.0
 */
MMModemMessaging *
mm_object_get_modem_messaging (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemMessaging *)mm_gdbus_object_get_modem_messaging (MM_GDBUS_OBJECT (self));
}

/**
 * mm_object_peek_modem_messaging: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem_messaging() but doesn't increase the reference count
 * on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModemMessaging or %NULL if @self does not
 * implement the interface. Do not free the returned object, it is owned by
 * @self.
 *
 * Since: 1.0
 */
MMModemMessaging *
mm_object_peek_modem_messaging (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemMessaging *)mm_gdbus_object_peek_modem_messaging (MM_GDBUS_OBJECT (self));
}

/*****************************************************************************/

/**
 * mm_object_get_modem_voice:
 * @self: A #MMObject.
 *
 * Gets the #MMModemVoice instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem.Modemvoice on @self, if any.
 *
 * Returns: (transfer full): A #MMModemVoice that must be freed with
 * g_object_unref() or %NULL if @self does not implement the interface.
 *
 * Since: 1.6
 */
MMModemVoice *
mm_object_get_modem_voice (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemVoice *)mm_gdbus_object_get_modem_voice (MM_GDBUS_OBJECT (self));
}

/**
 * mm_object_peek_modem_voice: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem_voice() but doesn't increase the reference count on
 * the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModemVoice or %NULL if @self does not
 * implement the interface. Do not free the returned object, it is owned by
 * @self.
 *
 * Since: 1.6
 */
MMModemVoice *
mm_object_peek_modem_voice (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemVoice *)mm_gdbus_object_peek_modem_voice (MM_GDBUS_OBJECT (self));
}

/*****************************************************************************/

/**
 * mm_object_get_modem_time:
 * @self: A #MMObject.
 *
 * Gets the #MMModemTime instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem.Time on @self, if any.
 *
 * Returns: (transfer full): A #MMModemTime that must be freed with
 * g_object_unref() or %NULL if @self does not implement the interface.
 *
 * Since: 1.0
 */
MMModemTime *
mm_object_get_modem_time (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemTime *)mm_gdbus_object_get_modem_time (MM_GDBUS_OBJECT (self));
}

/**
 * mm_object_peek_modem_time: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem_time() but doesn't increase the reference count on
 * the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModemTime or %NULL if @self does not implement
 * the interface. Do not free the returned object, it is owned by @self.
 *
 * Since: 1.0
 */
MMModemTime *
mm_object_peek_modem_time (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemTime *)mm_gdbus_object_peek_modem_time (MM_GDBUS_OBJECT (self));
}

/*****************************************************************************/

/**
 * mm_object_get_modem_firmware:
 * @self: A #MMObject.
 *
 * Gets the #MMModemFirmware instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem.Firmware on @self, if any.
 *
 * Returns: (transfer full): A #MMModemFirmware that must be freed with
 * g_object_unref() or %NULL if @self does not implement the interface.
 *
 * Since: 1.0
 */
MMModemFirmware *
mm_object_get_modem_firmware (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemFirmware *)mm_gdbus_object_get_modem_firmware (MM_GDBUS_OBJECT (self));
}

/**
 * mm_object_peek_modem_firmware: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem_firmware() but doesn't increase the reference count
 * on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModemFirmware or %NULL if @self does not
 * implement the interface. Do not free the returned object, it is owned by
 * @self.
 *
 * Since: 1.0
 */
MMModemFirmware *
mm_object_peek_modem_firmware (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemFirmware *)mm_gdbus_object_peek_modem_firmware (MM_GDBUS_OBJECT (self));
}

/*****************************************************************************/

/**
 * mm_object_get_modem_signal:
 * @self: A #MMObject.
 *
 * Gets the #MMModemSignal instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem.Signal on @self, if any.
 *
 * Returns: (transfer full): A #MMModemSignal that must be freed with
 * g_object_unref() or %NULL if @self does not implement the interface.
 *
 * Since: 1.2
 */
MMModemSignal *
mm_object_get_modem_signal (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemSignal *)mm_gdbus_object_get_modem_signal (MM_GDBUS_OBJECT (self));
}

/**
 * mm_object_peek_modem_signal: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem_signal() but doesn't increase the reference count on
 * the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModemSignal or %NULL if @self does not
 * implement the interface. Do not free the returned object, it is owned by
 * @self.
 *
 * Since: 1.2
 */
MMModemSignal *
mm_object_peek_modem_signal (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemSignal *)mm_gdbus_object_peek_modem_signal (MM_GDBUS_OBJECT (self));
}

/*****************************************************************************/

/**
 * mm_object_get_modem_oma:
 * @self: A #MMObject.
 *
 * Gets the #MMModemOma instance for the D-Bus interface
 * org.freedesktop.ModemManager1.Modem.Oma on @self, if any.
 *
 * Returns: (transfer full): A #MMModemOma that must be freed with
 * g_object_unref() or %NULL if @self does not implement the interface.
 *
 * Since: 1.2
 */
MMModemOma *
mm_object_get_modem_oma (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemOma *)mm_gdbus_object_get_modem_oma (MM_GDBUS_OBJECT (self));
}

/**
 * mm_object_peek_modem_oma: (skip)
 * @self: A #MMObject.
 *
 * Like mm_object_get_modem_oma() but doesn't increase the reference count on
 * the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another
 * thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModemOma or %NULL if @self does not implement
 * the interface. Do not free the returned object, it is owned by @self.
 *
 * Since: 1.2
 */
MMModemOma *
mm_object_peek_modem_oma (MMObject *self)
{
    g_return_val_if_fail (MM_IS_OBJECT (MM_GDBUS_OBJECT (self)), NULL);

    return (MMModemOma *)mm_gdbus_object_peek_modem_oma (MM_GDBUS_OBJECT (self));
}

/*****************************************************************************/

static void
mm_object_init (MMObject *self)
{
}

static void
mm_object_class_init (MMObjectClass *object_class)
{
}
