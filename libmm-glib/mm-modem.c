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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include <gio/gio.h>

#include "mm-modem.h"

/**
 * mm_modem_get_path:
 * @self: A #MMModem.
 *
 * Gets the DBus path of the #MMModem object.
 *
 * Returns: (transfer none): The DBus path of the #MMModem object.
 */
const gchar *
mm_modem_get_path (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    return g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
}

/**
 * mm_modem_get_sim_path:
 * @self: A #MMModem.
 *
 * Gets the DBus path of the #MMSim handled in this #MMModem.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_sim_path() if on another thread.</warning>
 *
 * Returns: (transfer none): The DBus path of the #MMSim handled in this #MMModem, or %NULL if none available.
 */
const gchar *
mm_modem_get_sim_path (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    return (mm_gdbus_modem_get_sim (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_dup_sim_path:
 * @self: A #MMModem.
 *
 * Gets a copy of the DBus path of the #MMSim handled in this #MMModem.
 *
 * Returns: (transfer full): The DBus path of the #MMSim handled in this #MMModem, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_sim_path (MMModem *self)
{
    gchar *sim_path;
    MmGdbusModem *modem;

    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    modem = mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self));
    sim_path = mm_gdbus_modem_dup_sim (modem);
    g_object_unref (modem);
    return sim_path;
}

/**
 * mm_modem_get_modem_capabilities:
 * @self: A #MMModem.
 *
 * Gets the list of generic families of access technologies supported by this #MMModem.
 *
 * Not all capabilities are available at the same time however; some
 * modems require a firmware reload or other reinitialization to switch
 * between e.g. CDMA/EVDO and GSM/UMTS.
 *
 * Returns: A bitmask of #MMModemCapability flags.
 */
MMModemCapability
mm_modem_get_modem_capabilities (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), MM_MODEM_CAPABILITY_NONE);

    return (mm_gdbus_modem_get_modem_capabilities (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_current_capabilities:
 * @self: A #MMModem.
 *
 * Gets the list of generic families of access technologies supported by this #MMModem
 * without a firmware reload or reinitialization.
 *
 * Returns: A bitmask of #MMModemCapability flags.
 */
MMModemCapability
mm_modem_get_current_capabilities (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), MM_MODEM_CAPABILITY_NONE);

    return (mm_gdbus_modem_get_current_capabilities (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_max_bearers:
 * @self: a #MMModem.
 *
 * Gets the maximum number of defined packet data bearers this #MMModem supports.
 *
 * This is not the number of active/connected bearers the modem supports,
 * but simply the number of bearers that may be defined at any given time.
 * For example, POTS and CDMA2000-only devices support only one bearer,
 * while GSM/UMTS devices typically support three or more, and any
 * LTE-capable device (whether LTE-only, GSM/UMTS-capable, and/or
 * CDMA2000-capable) also typically support three or more.
 *
 * Returns: the maximum number of defined packet data bearers.
 */
guint
mm_modem_get_max_bearers (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), 0);

    return (mm_gdbus_modem_get_max_bearers (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_max_bearers:
 * @self: a #MMModem.
 *
 * Gets the maximum number of active packet data bearers this #MMModem supports.
 *
 * POTS and CDMA2000-only devices support one active bearer, while GSM/UMTS
 * and LTE-capable devices (including LTE/CDMA devices) typically support
 * at least two active bearers.
 *
 * Returns: the maximum number of defined packet data bearers.
 */
guint
mm_modem_get_max_active_bearers (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), 0);

    return (mm_gdbus_modem_get_max_active_bearers (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_manufacturer:
 * @self: A #MMModem.
 *
 * Gets the equipment manufacturer, as reported by this #MMModem.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_manufacturer() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment manufacturer, or %NULL if none available.
 */
const gchar *
mm_modem_get_manufacturer (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    return (mm_gdbus_modem_get_manufacturer (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_dup_manufacturer:
 * @self: A #MMModem.
 *
 * Gets a copy of the equipment manufacturer, as reported by this #MMModem.
 *
 * Returns: (transfer full): The equipment manufacturer, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_manufacturer (MMModem *self)
{
    gchar *manufacturer;
    MmGdbusModem *modem;

    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    modem = mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self));
    manufacturer = mm_gdbus_modem_dup_manufacturer (modem);
    g_object_unref (modem);
    return manufacturer;
}

/**
 * mm_modem_get_model:
 * @self: A #MMModem.
 *
 * Gets the equipment model, as reported by this #MMModem.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_model() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment model, or %NULL if none available.
 */
const gchar *
mm_modem_get_model (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    return (mm_gdbus_modem_get_model (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_dup_model:
 * @self: A #MMModem.
 *
 * Gets a copy of the equipment model, as reported by this #MMModem.
 *
 * Returns: (transfer full): The equipment model, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_model (MMModem *self)
{
    gchar *model;
    MmGdbusModem *modem;

    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    modem = mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self));
    model = mm_gdbus_modem_dup_model (modem);
    g_object_unref (modem);
    return model;
}

/**
 * mm_modem_get_revision:
 * @self: A #MMModem.
 *
 * Gets the equipment revision, as reported by this #MMModem.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_revision() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment revision, or %NULL if none available.
 */
const gchar *
mm_modem_get_revision (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    return (mm_gdbus_modem_get_revision (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_dup_revision:
 * @self: A #MMModem.
 *
 * Gets a copy of the equipment revision, as reported by this #MMModem.
 *
 * Returns: (transfer full): The equipment revision, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_revision (MMModem *self)
{
    gchar *revision;
    MmGdbusModem *modem;

    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    modem = mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self));
    revision = mm_gdbus_modem_dup_revision (modem);
    g_object_unref (modem);
    return revision;
}

/**
 * mm_modem_get_device_identifier:
 * @self: A #MMModem.
 *
 * Gets a best-effort device identifier based on various device information like
 * model name, firmware revision, USB/PCI/PCMCIA IDs, and other properties.
 *
 * This ID is not guaranteed to be unique and may be shared between
 * identical devices with the same firmware, but is intended to be "unique
 * enough" for use as a casual device identifier for various user
 * experience operations.
 *
 * This is not the device's IMEI or ESN since those may not be available
 * before unlocking the device via a PIN.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_device_identifier() if on another thread.</warning>
 *
 * Returns: (transfer none): The device identifier, or %NULL if none available.
 */
const gchar *
mm_modem_get_device_identifier (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    return (mm_gdbus_modem_get_device_identifier (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_dup_device_identifier:
 * @self: A #MMModem.
 *
 * Gets a copy of a best-effort device identifier based on various device information
 * like model name, firmware revision, USB/PCI/PCMCIA IDs, and other properties.
 *
 * This ID is not guaranteed to be unique and may be shared between
 * identical devices with the same firmware, but is intended to be "unique
 * enough" for use as a casual device identifier for various user
 * experience operations.
 *
 * This is not the device's IMEI or ESN since those may not be available
 * before unlocking the device via a PIN.
 *
 * Returns: (transfer full): The device identifier, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_device_identifier (MMModem *self)
{
    gchar *device_identifier;
    MmGdbusModem *modem;

    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    modem = mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self));
    device_identifier = mm_gdbus_modem_dup_device_identifier (modem);
    g_object_unref (modem);
    return device_identifier;
}

/**
 * mm_modem_get_device:
 * @self: A #MMModem.
 *
 * Gets the physical modem device reference (ie, USB, PCI, PCMCIA device), which
 * may be dependent upon the operating system.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_device() if on another thread.</warning>
 *
 * Returns: (transfer none): The device, or %NULL if none available.
 */
const gchar *
mm_modem_get_device (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    return (mm_gdbus_modem_get_device (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_dup_device:
 * @self: A #MMModem.
 *
 * Gets a copy of the physical modem device reference (ie, USB, PCI, PCMCIA device), which
 * may be dependent upon the operating system.
 *
 * Returns: (transfer full): The device, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_device (MMModem *self)
{
    gchar *device;
    MmGdbusModem *modem;

    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    modem = mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self));
    device = mm_gdbus_modem_dup_device (modem);
    g_object_unref (modem);
    return device;
}

/**
 * mm_modem_get_driver:
 * @self: A #MMModem.
 *
 * Gets the Operating System device driver handling communication with the modem
 * hardware.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_driver() if on another thread.</warning>
 *
 * Returns: (transfer none): The driver, or %NULL if none available.
 */
const gchar *
mm_modem_get_driver (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    return (mm_gdbus_modem_get_driver (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_dup_driver:
 * @self: A #MMModem.
 *
 * Gets a copy of the Operating System device driver handling communication with the modem
 * hardware.
 *
 * Returns: (transfer full): The driver, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_driver (MMModem *self)
{
    gchar *driver;
    MmGdbusModem *modem;

    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    modem = mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self));
    driver = mm_gdbus_modem_dup_driver (modem);
    g_object_unref (modem);
    return driver;
}

/**
 * mm_modem_get_plugin:
 * @self: A #MMModem.
 *
 * Gets the name of the plugin handling this #MMModem.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_plugin() if on another thread.</warning>
 *
 * Returns: (transfer none): The name of the plugin, or %NULL if none available.
 */
const gchar *
mm_modem_get_plugin (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    return (mm_gdbus_modem_get_plugin (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_dup_plugin:
 * @self: A #MMModem.
 *
 * Gets a copy of the name of the plugin handling this #MMModem.
 *
 * Returns: (transfer full): The name of the plugin, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_plugin (MMModem *self)
{
    gchar *plugin;
    MmGdbusModem *modem;

    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    modem = mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self));
    plugin = mm_gdbus_modem_dup_plugin (modem);
    g_object_unref (modem);
    return plugin;
}

/**
 * mm_modem_get_equipment_identifier:
 * @self: A #MMModem.
 *
 * Gets the identity of the #MMModem.
 *
 * This will be the IMEI number for GSM devices and the hex-format ESN/MEID
 * for CDMA devices.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_equipment_identifier() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment identifier, or %NULL if none available.
 */
const gchar *
mm_modem_get_equipment_identifier (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    return (mm_gdbus_modem_get_equipment_identifier (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_dup_equipment_identifier:
 * @self: A #MMModem.
 *
 * Gets a copy of the identity of the #MMModem.
 *
 * This will be the IMEI number for GSM devices and the hex-format ESN/MEID
 * for CDMA devices.
 *
 * Returns: (transfer full): The equipment identifier, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_equipment_identifier (MMModem *self)
{
    gchar *equipment_identifier;
    MmGdbusModem *modem;

    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), NULL);

    modem = mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self));
    equipment_identifier = mm_gdbus_modem_dup_equipment_identifier (modem);
    g_object_unref (modem);
    return equipment_identifier;
}

/**
 * mm_modem_get_unlock_required:
 * @self: A #MMModem.
 *
 * Gets current lock state of the #MMModemm.
 *
 * Returns: A #MMModemLock value, specifying the current lock state.
 */
MMModemLock
mm_modem_get_unlock_required (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), MM_MODEM_LOCK_UNKNOWN);

    return (mm_gdbus_modem_get_unlock_required (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_unlock_retries:
 * @self: A #MMModem.
 *
 * Gets the number of unlock retries remaining for the lock code given by the
 * UnlockRequired property (if any), or 999 if the device does not support reporting
 * unlock retries.
 *
 * Returns: The number of unlock retries.
 */
guint
mm_modem_get_unlock_retries (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), 0);

    return (mm_gdbus_modem_get_unlock_retries (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_state:
 * @self: A #MMModem.
 *
 * Gets the overall state of the #MMModem.
 *
 * Returns: A #MMModemState value.
 */
MMModemState
mm_modem_get_state (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), MM_MODEM_STATE_UNKNOWN);

    return (mm_gdbus_modem_get_state (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_access_technology:
 * @self: A #MMModem.
 *
 * Gets the current network access technology used by the #MMModem to communicate
 * with the network.
 *
 * Returns: A ##MMModemAccessTech value.
 */
MMModemAccessTech
mm_modem_get_access_technology (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), MM_MODEM_ACCESS_TECH_UNKNOWN);

    return (mm_gdbus_modem_get_access_technology (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_signal_quality:
 * @self: A #MMModem.
 * @recent: (out): Return location for the flag specifying if the signal quality value was recent or not.
 *
 * Gets the signal quality value in percent (0 - 100) of the dominant access technology
 * the #MMModem is using to communicate with the network.
 *
 * Always 0 for POTS devices.
 *
 * Returns: The signal quality.
 */
guint
mm_modem_get_signal_quality (MMModem *self,
                             gboolean *recent)
{
    GVariant *variant;
    gboolean is_recent = FALSE;
    guint quality = 0;

    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), 0);

    variant = (mm_gdbus_modem_get_signal_quality (
                   mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
    if (variant) {
        g_variant_get (variant,
                       "(ub)",
                       &quality,
                       &is_recent);
    }

    if (recent)
        *recent = is_recent;
    return quality;
}

/**
 * mm_modem_get_supported_modes:
 * @self: A #MMModem.
 *
 * Gets the list of modes specifying the access technologies supported by the #MMModem.
 *
 * For POTS devices, only #MM_MODEM_MODE_ANY will be returned.
 *
 * Returns: A bitmask of #MMModemMode values.
 */
MMModemMode
mm_modem_get_supported_modes (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), MM_MODEM_MODE_NONE);

    return (mm_gdbus_modem_get_supported_modes (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_allowed_modes:
 * @self: A #MMModem.
 *
 * Gets the list of modes specifying the access technologies (eg 2G/3G/4G preference)
 * the #MMModem is currently allowed to use when connecting to a network.
 *
 * For POTS devices, only the #MM_MODEM_MODE_ANY is supported.
 *
 * Returns: A bitmask of #MMModemMode values.
 */
MMModemMode
mm_modem_get_allowed_modes (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), MM_MODEM_MODE_NONE);

    return (mm_gdbus_modem_get_allowed_modes (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_preferred_mode:
 * @self: A #MMModem.
 *
 * Get the preferred access technology (eg 2G/3G/4G preference), among
 * the ones defined in the allowed modes.
 *
 * Returns: A single #MMModemMode value.
 */
MMModemMode
mm_modem_get_preferred_mode (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), MM_MODEM_MODE_NONE);

    return (mm_gdbus_modem_get_preferred_mode (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_supported_bands:
 * @self: A #MMModem.
 *
 * Gets the list of radio frequency and technology bands supported by the #MMModem.
 *
 * For POTS devices, only #MM_MODEM_BAND_ANY will be returned.
 *
 * Returns: A bitmask of #MMModemBand values.
 */
MMModemBand
mm_modem_get_supported_bands (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), MM_MODEM_MODE_NONE);

    return (mm_gdbus_modem_get_supported_bands (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

/**
 * mm_modem_get_allowed_bands:
 * @self: A #MMModem.
 *
 * Gets the list of radio frequency and technology bands the #MMModem is currently
 * allowed to use when connecting to a network.
 *
 * For POTS devices, only the #MM_MODEM_BAND_ANY band is supported.
 *
 * Returns: A bitmask of #MMModemBand values.
 */
MMModemBand
mm_modem_get_allowed_bands (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), MM_MODEM_MODE_NONE);

    return (mm_gdbus_modem_get_allowed_bands (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self))));
}

#define BOOL_REPLY_READY_FN(NAME)                                       \
    static void                                                         \
    common_##NAME##_ready (MmGdbusModem *modem_iface_proxy,             \
                           GAsyncResult *res,                           \
                           GSimpleAsyncResult *simple)                  \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        if (mm_gdbus_modem_call_##NAME##_finish (                       \
                modem_iface_proxy,                                      \
                res,                                                    \
                &error))                                                \
            g_simple_async_result_take_error (simple, error);           \
        else                                                            \
            g_simple_async_result_set_op_res_gboolean (simple, TRUE);   \
                                                                        \
        /* balance ref count */                                         \
        g_object_unref (modem_iface_proxy);                             \
        g_simple_async_result_complete (simple);                        \
        g_object_unref (simple);                                        \
    }

#define BOOL_REPLY_FINISH_FN(NAME)                                      \
    gboolean                                                            \
    mm_modem_##NAME##_finish (MMModem *self,                            \
                              GAsyncResult *res,                        \
                              GError **error)                           \
    {                                                                   \
        g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), FALSE);        \
                                                                        \
        if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error)) \
            return FALSE;                                               \
                                                                        \
        return g_simple_async_result_get_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res)); \
    }

BOOL_REPLY_READY_FN  (enable)

/**
 * mm_modem_enable_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_enable().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_enable().
 *
 * Returns: (skip): %TRUE if the modem was properly enabled, %FALSE if @error is set.
 */
BOOL_REPLY_FINISH_FN (enable)

/**
 * mm_modem_enable:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously tries to enable the #MMModem. When enabled, the modem's radio is
 * powered on and data sessions, voice calls, location services, and Short Message
 * Service may be available.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_enable_finish() to get the result of the operation.
 *
 * See mm_modem_enable_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_enable (MMModem *self,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_return_if_fail (MM_GDBUS_IS_OBJECT (self));

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_modem_enable);
    mm_gdbus_modem_call_enable (
        mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self)), /* unref later */
        TRUE,
        cancellable,
        (GAsyncReadyCallback)common_enable_ready,
        result);
}

/**
 * mm_modem_enable_sync:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously tries to enable the #MMModem. When enabled, the modem's radio is
 * powered on and data sessions, voice calls, location services, and Short Message
 * Service may be available.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_enable()
 * for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the modem was properly enabled, %FALSE if @error is set.
 */
gboolean
mm_modem_enable_sync (MMModem *self,
                      GCancellable *cancellable,
                      GError **error)
{
    return (mm_gdbus_modem_call_enable_sync (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self)),
                TRUE,
                cancellable,
                error));
}

static void
modem_disable_ready (MmGdbusModem *modem_iface_proxy,
                     GAsyncResult *res,
                     GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (mm_gdbus_modem_call_enable_finish (
            modem_iface_proxy,
            res,
            &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);

    /* balance ref count */
    g_object_unref (modem_iface_proxy);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

/**
 * mm_modem_disable_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_disable().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_disable().
 *
 * Returns: (skip): %TRUE if the modem was properly disabled, %FALSE if @error is set.
 */
BOOL_REPLY_FINISH_FN (disable)

/**
 * mm_modem_disable:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously tries to disable the #MMModem. When disabled, the modem enters
 * low-power state and no network-related operations are available.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_disable_finish() to get the result of the operation.
 *
 * See mm_modem_disable_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_disable (MMModem *self,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_return_if_fail (MM_GDBUS_IS_OBJECT (self));

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_modem_disable);
    mm_gdbus_modem_call_enable (
        mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self)), /* unref later */
        FALSE,
        cancellable,
        (GAsyncReadyCallback)modem_disable_ready,
        result);
}

/**
 * mm_modem_disable_sync:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously tries to disable the #MMModem. When disabled, the modem enters
 * low-power state and no network-related operations are available.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_disable()
 * for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the modem was properly disabled, %FALSE if @error is set.
 */
gboolean
mm_modem_disable_sync (MMModem *self,
                      GCancellable *cancellable,
                      GError **error)
{
    return (mm_gdbus_modem_call_enable_sync (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self)),
                FALSE,
                cancellable,
                error));
}

static void
create_bearer_ready (MmGdbusModem *modem_iface_proxy,
                     GAsyncResult *res,
                     GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    gchar *path = NULL;

    if (mm_gdbus_modem_call_create_bearer_finish (
            modem_iface_proxy,
            &path,
            res,
            &error)) {
        g_simple_async_result_take_error (simple, error);
        g_free (path);
    }
    else
        g_simple_async_result_set_op_res_gpointer (simple, path, NULL);

    /* balance ref count */
    g_object_unref (modem_iface_proxy);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_modem_create_bearer (MMModem *self,
                        GVariant *properties,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_return_if_fail (MM_GDBUS_IS_OBJECT (self));

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_modem_create_bearer);
    mm_gdbus_modem_call_create_bearer (
        mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self)), /* unref later */
        properties,
        cancellable,
        (GAsyncReadyCallback)create_bearer_ready,
        result);
}

gboolean
mm_modem_create_bearer_finish (MMModem *self,
                               gchar **out_path,
                               GAsyncResult *res,
                               GError **error)
{
    gchar *path;

    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (self), FALSE);

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    path = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    if (!out_path)
        g_free (path);
    else
        *out_path = path;
    return TRUE;
}

gboolean
mm_modem_create_bearer_sync (MMModem *self,
                             GVariant *properties,
                             gchar **out_path,
                             GCancellable *cancellable,
                             GError **error)
{
    return (mm_gdbus_modem_call_create_bearer_sync (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self)),
                properties,
                out_path,
                cancellable,
                error));
}

BOOL_REPLY_READY_FN  (delete_bearer)
BOOL_REPLY_FINISH_FN (delete_bearer)

void
mm_modem_delete_bearer (MMModem *self,
                        const gchar *bearer,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_return_if_fail (MM_GDBUS_IS_OBJECT (self));

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_modem_delete_bearer);
    mm_gdbus_modem_call_delete_bearer (
        mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self)), /* unref later */
        bearer,
        cancellable,
        (GAsyncReadyCallback)common_delete_bearer_ready,
        result);
}

gboolean
mm_modem_delete_bearer_sync (MMModem *self,
                             const gchar *bearer,
                             GCancellable *cancellable,
                             GError **error)
{
    return (mm_gdbus_modem_call_delete_bearer_sync (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self)),
                bearer,
                cancellable,
                error));
}

BOOL_REPLY_READY_FN  (reset)
BOOL_REPLY_FINISH_FN (reset)

void
mm_modem_reset (MMModem *self,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_return_if_fail (MM_GDBUS_IS_OBJECT (self));

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_modem_reset);
    mm_gdbus_modem_call_reset (
        mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self)), /* unref later */
        cancellable,
        (GAsyncReadyCallback)common_reset_ready,
        result);
}

gboolean
mm_modem_reset_sync (MMModem *self,
                     GCancellable *cancellable,
                     GError **error)
{
    return (mm_gdbus_modem_call_reset_sync (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self)),
                cancellable,
                error));
}

BOOL_REPLY_READY_FN  (factory_reset)
BOOL_REPLY_FINISH_FN (factory_reset)

void
mm_modem_factory_reset (MMModem *self,
                        const gchar *code,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_return_if_fail (MM_GDBUS_IS_OBJECT (self));

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_modem_factory_reset);
    mm_gdbus_modem_call_factory_reset (
        mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self)), /* unref later */
        code,
        cancellable,
        (GAsyncReadyCallback)common_factory_reset_ready,
        result);
}

gboolean
mm_modem_factory_reset_sync (MMModem *self,
                             const gchar *code,
                             GCancellable *cancellable,
                             GError **error)
{
    return (mm_gdbus_modem_call_factory_reset_sync (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self)),
                code,
                cancellable,
                error));
}

BOOL_REPLY_READY_FN  (set_allowed_modes)
BOOL_REPLY_FINISH_FN (set_allowed_modes)

void
mm_modem_set_allowed_modes (MMModem *self,
                            MMModemMode modes,
                            MMModemMode preferred,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_return_if_fail (MM_GDBUS_IS_OBJECT (self));

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_modem_set_allowed_modes);
    mm_gdbus_modem_call_set_allowed_modes (
        mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self)), /* unref later */
        modes,
        preferred,
        cancellable,
        (GAsyncReadyCallback)common_set_allowed_modes_ready,
        result);
}

gboolean
mm_modem_set_allowed_modes_sync (MMModem *self,
                                 MMModemMode modes,
                                 MMModemMode preferred,
                                 GCancellable *cancellable,
                                 GError **error)
{
    return (mm_gdbus_modem_call_set_allowed_modes_sync (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self)),
                modes,
                preferred,
                cancellable,
                error));
}

BOOL_REPLY_READY_FN  (set_allowed_bands)
BOOL_REPLY_FINISH_FN (set_allowed_bands)

void
mm_modem_set_allowed_bands (MMModem *self,
                            MMModemBand bands,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_return_if_fail (MM_GDBUS_IS_OBJECT (self));

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_modem_set_allowed_bands);
    mm_gdbus_modem_call_set_allowed_bands (
        mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self)), /* unref later */
        bands,
        cancellable,
        (GAsyncReadyCallback)common_set_allowed_bands_ready,
        result);
}

gboolean
mm_modem_set_allowed_bands_sync (MMModem *self,
                                 MMModemBand bands,
                                 GCancellable *cancellable,
                                 GError **error)
{
    return (mm_gdbus_modem_call_set_allowed_bands_sync (
                mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self)),
                bands,
                cancellable,
                error));
}
