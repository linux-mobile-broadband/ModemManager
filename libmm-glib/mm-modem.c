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
#include <string.h>

#include "mm-common-helpers.h"
#include "mm-errors-types.h"
#include "mm-helpers.h"
#include "mm-modem.h"

/**
 * SECTION: mm-modem
 * @title: MMModem
 * @short_description: The Modem interface
 *
 * The #MMModem is an object providing access to the methods, signals and
 * properties of the Modem interface.
 *
 * When the modem is exposed and available in the bus, it is ensured that at
 * least this interface is also available.
 */

G_DEFINE_TYPE (MMModem, mm_modem, MM_GDBUS_TYPE_MODEM_PROXY)

struct _MMModemPrivate {
    /* Ports */
    GMutex ports_mutex;
    guint ports_id;
    GArray *ports;

    /* UnlockRetries */
    GMutex unlock_retries_mutex;
    guint unlock_retries_id;
    MMUnlockRetries *unlock_retries;

    /* Supported Modes */
    GMutex supported_modes_mutex;
    guint supported_modes_id;
    GArray *supported_modes;

    /* Supported Capabilities */
    GMutex supported_capabilities_mutex;
    guint supported_capabilities_id;
    GArray *supported_capabilities;

    /* Supported Bands */
    GMutex supported_bands_mutex;
    guint supported_bands_id;
    GArray *supported_bands;

    /* Current Bands */
    GMutex current_bands_mutex;
    guint current_bands_id;
    GArray *current_bands;
};

/*****************************************************************************/

/**
 * mm_modem_get_path: (skip)
 * @self: A #MMModem.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object. Do not free
 * the returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_get_path (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_dup_path:
 * @self: A #MMModem.
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
mm_modem_dup_path (MMModem *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_get_sim_path: (skip)
 * @self: A #MMModem.
 *
 * Gets the DBus path of the #MMSim handled in this #MMModem.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_sim_path() if on another thread.</warning>
 *
 * Returns: (transfer none): The DBus path of the #MMSim handled in this
 * #MMModem, or %NULL if none available. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_get_sim_path (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (mm_gdbus_modem_get_sim (MM_GDBUS_MODEM (self)));
}

/**
 * mm_modem_dup_sim_path:
 * @self: A #MMModem.
 *
 * Gets a copy of the DBus path of the #MMSim handled in this #MMModem.
 *
 * Returns: (transfer full): The DBus path of the #MMSim handled in this
 * #MMModem, or %NULL if none available. The returned value should be freed
 * with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_dup_sim_path (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_sim (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

/**
 * mm_modem_get_sim_slot_paths:
 * @self: A #MMModem.
 *
 * Gets the DBus paths of the #MMSim objects available in the different SIM
 * slots handled in this #MMModem. If a given SIM slot at a given index doesn't
 * have a SIM card available, an empty object path will be given. This list
 * includes the currently active SIM object path.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_sim_slot_paths() if on another thread.</warning>
 *
 * Returns: (transfer none): The DBus paths of the #MMSim objects handled in
 * this #MMModem, or %NULL if none available. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.16
 */
const gchar * const *
mm_modem_get_sim_slot_paths (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    return mm_gdbus_modem_get_sim_slots (MM_GDBUS_MODEM (self));
}

/**
 * mm_modem_dup_sim_slot_paths:
 * @self: A #MMModem.
 *
 * Gets a copy of the DBus paths of the #MMSim objects available in the
 * different SIM slots handled in this #MMModem. If a given SIM slot at a given
 * index doesn't have a SIM card available, an empty object path will be given.
 * This list includes the currently active SIM object path.
 *
 * Returns: (transfer full): The DBus paths of the #MMSim objects handled in
 * this #MMModem, or %NULL if none available. The returned value should be
 * freed with g_strfreev().
 *
 * Since: 1.16
 */
gchar **
mm_modem_dup_sim_slot_paths (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    return mm_gdbus_modem_dup_sim_slots (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

/**
 * mm_modem_get_primary_sim_slot:
 * @self: A #MMModem.
 *
 * Gets the SIM slot number of the primary active SIM.
 *
 * Returns: slot number, in the [1,N] range.
 *
 * Since: 1.16
 */
guint
mm_modem_get_primary_sim_slot (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), 0);

    return mm_gdbus_modem_get_primary_sim_slot (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

static void
supported_capabilities_updated (MMModem *self,
                                GParamSpec *pspec)
{
    g_mutex_lock (&self->priv->supported_capabilities_mutex);
    {
        GVariant *dictionary;

        if (self->priv->supported_capabilities)
            g_array_unref (self->priv->supported_capabilities);

        dictionary = mm_gdbus_modem_get_supported_capabilities (MM_GDBUS_MODEM (self));
        self->priv->supported_capabilities = (dictionary ?
                                              mm_common_capability_combinations_variant_to_garray (dictionary) :
                                              NULL);
    }
    g_mutex_unlock (&self->priv->supported_capabilities_mutex);
}

static gboolean
ensure_internal_supported_capabilities (MMModem *self,
                                        MMModemCapability **dup_capabilities,
                                        guint *dup_capabilities_n)
{
    gboolean ret;

    g_mutex_lock (&self->priv->supported_capabilities_mutex);
    {
        /* If this is the first time ever asking for the array, setup the
         * update listener and the initial array, if any. */
        if (!self->priv->supported_capabilities_id) {
            GVariant *dictionary;

            dictionary = mm_gdbus_modem_dup_supported_capabilities (MM_GDBUS_MODEM (self));
            if (dictionary) {
                self->priv->supported_capabilities = mm_common_capability_combinations_variant_to_garray (dictionary);
                g_variant_unref (dictionary);
            }

            /* No need to clear this signal connection when freeing self */
            self->priv->supported_capabilities_id =
                g_signal_connect (self,
                                  "notify::supported-capabilities",
                                  G_CALLBACK (supported_capabilities_updated),
                                  NULL);
        }

        if (!self->priv->supported_capabilities)
            ret = FALSE;
        else {
            ret = TRUE;

            if (dup_capabilities && dup_capabilities_n) {
                *dup_capabilities_n = self->priv->supported_capabilities->len;
                if (self->priv->supported_capabilities->len > 0) {
                    *dup_capabilities = g_malloc (sizeof (MMModemCapability) * self->priv->supported_capabilities->len);
                    memcpy (*dup_capabilities, self->priv->supported_capabilities->data, sizeof (MMModemCapability) * self->priv->supported_capabilities->len);
                } else
                    *dup_capabilities = NULL;
            }
        }
    }
    g_mutex_unlock (&self->priv->supported_capabilities_mutex);

    return ret;
}

/**
 * mm_modem_get_supported_capabilities:
 * @self: A #MMModem.
 * @capabilities: (out) (array length=n_capabilities): Return location for the
 *  array of #MMModemCapability values. The returned array should be freed with
 *  g_free() when no longer needed.
 * @n_capabilities: (out): Return location for the number of values in
 *  @capabilities.
 *
 * Gets the list of combinations of generic families of access technologies
 * supported by this #MMModem.
 *
 * Returns: %TRUE if @capabilities and @n_capabilities are set, %FALSE
 * otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_modem_get_supported_capabilities (MMModem *self,
                                     MMModemCapability **capabilities,
                                     guint *n_capabilities)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (capabilities != NULL, FALSE);
    g_return_val_if_fail (n_capabilities != NULL, FALSE);

    return ensure_internal_supported_capabilities (self, capabilities, n_capabilities);
}

/**
 * mm_modem_peek_supported_capabilities:
 * @self: A #MMModem.
 * @capabilities: (out) (array length=n_capabilities): Return location for the
 *  array of #MMModemCapability values. Do not free the returned array, it is
 *  owned by @self.
 * @n_capabilities: (out): Return location for the number of values in
 * @capabilities.
 *
 * Gets the list of combinations of generic families of access technologies
 * supported by this #MMModem.
 *
 * Returns: %TRUE if @capabilities and @n_capabilities are set, %FALSE
 * otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_modem_peek_supported_capabilities (MMModem *self,
                                      const MMModemCapability **capabilities,
                                      guint *n_capabilities)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (capabilities != NULL, FALSE);
    g_return_val_if_fail (n_capabilities != NULL, FALSE);

    if (!ensure_internal_supported_capabilities (self, NULL, NULL))
        return FALSE;

    *n_capabilities = self->priv->supported_capabilities->len;
    *capabilities = (MMModemCapability *)self->priv->supported_capabilities->data;
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_modem_get_current_capabilities:
 * @self: A #MMModem.
 *
 * Gets the list of generic families of access technologies supported by this
 * #MMModem without a firmware reload or reinitialization.
 *
 * Returns: A bitmask of #MMModemCapability flags.
 *
 * Since: 1.0
 */
MMModemCapability
mm_modem_get_current_capabilities (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), MM_MODEM_CAPABILITY_NONE);

    return mm_gdbus_modem_get_current_capabilities (MM_GDBUS_MODEM (self));
}

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
 */
guint
mm_modem_get_max_bearers (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), 0);

    return mm_gdbus_modem_get_max_bearers (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

/**
 * mm_modem_get_max_active_bearers:
 * @self: a #MMModem.
 *
 * Gets the maximum number of active packet data bearers this #MMModem supports.
 *
 * POTS and CDMA2000-only devices support one active bearer, while GSM/UMTS
 * and LTE-capable devices (including LTE/CDMA devices) typically support
 * at least two active bearers.
 *
 * Returns: the maximum number of defined packet data bearers.
 *
 * Since: 1.0
 */
guint
mm_modem_get_max_active_bearers (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), 0);

    return mm_gdbus_modem_get_max_active_bearers (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

/**
 * mm_modem_get_bearer_paths:
 * @self: A #MMModem.
 *
 * Gets the DBus paths of the #MMBearer handled in this #MMModem.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_bearer_paths() if on another thread.</warning>
 *
 * Returns: (transfer none): The DBus paths of the #MMBearer handled in this
 * #MMModem, or %NULL if none available. Do not free the returned value, it
 * belongs to @self.
 *
 * Since: 1.0
 */
const gchar * const *
mm_modem_get_bearer_paths (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    return mm_gdbus_modem_get_bearers (MM_GDBUS_MODEM (self));
}

/**
 * mm_modem_dup_bearer_paths:
 * @self: A #MMModem.
 *
 * Gets a copy of the DBus paths of the #MMBearer handled in this #MMModem.
 *
 * Returns: (transfer full): The DBus paths of the #MMBearer handled in this
 * #MMModem, or %NULL if none available. The returned value should be freed
 * with g_strfreev().
 *
 * Since: 1.0
 */
gchar **
mm_modem_dup_bearer_paths (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    return mm_gdbus_modem_dup_bearers (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

/**
 * mm_modem_get_manufacturer:
 * @self: A #MMModem.
 *
 * Gets the equipment manufacturer, as reported by this #MMModem.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_manufacturer() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment manufacturer, or %NULL if none
 * available. Do not free the returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_get_manufacturer (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_manufacturer (MM_GDBUS_MODEM (self)));
}

/**
 * mm_modem_dup_manufacturer:
 * @self: A #MMModem.
 *
 * Gets a copy of the equipment manufacturer, as reported by this #MMModem.
 *
 * Returns: (transfer full): The equipment manufacturer, or %NULL if none
 * available. The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_dup_manufacturer (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_manufacturer (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

/**
 * mm_modem_get_model:
 * @self: A #MMModem.
 *
 * Gets the equipment model, as reported by this #MMModem.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_model() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment model, or %NULL if none available.
 * Do not free the returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_get_model (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_model (MM_GDBUS_MODEM (self)));
}

/**
 * mm_modem_dup_model:
 * @self: A #MMModem.
 *
 * Gets a copy of the equipment model, as reported by this #MMModem.
 *
 * Returns: (transfer full): The equipment model, or %NULL if none available.
 * The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_dup_model (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_model (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

/**
 * mm_modem_get_revision:
 * @self: A #MMModem.
 *
 * Gets the equipment revision, as reported by this #MMModem.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_revision() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment revision, or %NULL if none available.
 * Do not free the returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_get_revision (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_revision (MM_GDBUS_MODEM (self)));
}

/**
 * mm_modem_dup_revision:
 * @self: A #MMModem.
 *
 * Gets a copy of the equipment revision, as reported by this #MMModem.
 *
 * Returns: (transfer full): The equipment revision, or %NULL if none available.
 * The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_dup_revision (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_revision (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

/**
 * mm_modem_get_carrier_configuration:
 * @self: A #MMModem.
 *
 * Gets the carrier-specific configuration (MCFG) in use, as reported by this
 * #MMModem.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_carrier_configuration() if on another thread.</warning>
 *
 * Returns: (transfer none): The carrier configuration, or %NULL if none
 * available. Do not free the returned value, it belongs to @self.
 *
 * Since: 1.12
 */
const gchar *
mm_modem_get_carrier_configuration (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_carrier_configuration (MM_GDBUS_MODEM (self)));
}

/**
 * mm_modem_dup_carrier_configuration:
 * @self: A #MMModem.
 *
 * Gets a copy of the carrier-specific configuration (MCFG) in use, as reported
 * by this #MMModem.
 *
 * Returns: (transfer full): The carrier configuration, or %NULL if none
 * available. The returned value should be freed with g_free().
 *
 * Since: 1.12
 */
gchar *
mm_modem_dup_carrier_configuration (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_carrier_configuration (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

/**
 * mm_modem_get_carrier_configuration_revision:
 * @self: A #MMModem.
 *
 * Gets the carrier-specific configuration revision in use, as reported by this
 * #MMModem.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_carrier_configuration() if on another thread.</warning>
 *
 * Returns: (transfer none): The carrier configuration revision, or %NULL if
 * none available. Do not free the returned value, it belongs to @self.
 *
 * Since: 1.12
 */
const gchar *
mm_modem_get_carrier_configuration_revision (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_carrier_configuration_revision (MM_GDBUS_MODEM (self)));
}

/**
 * mm_modem_dup_carrier_configuration_revision:
 * @self: A #MMModem.
 *
 * Gets a copy of the carrier-specific configuration revision in use, as
 * reported by this #MMModem.
 *
 * Returns: (transfer full): The carrier configuration revision, or %NULL if
 * none available. The returned value should be freed with g_free().
 *
 * Since: 1.12
 */
gchar *
mm_modem_dup_carrier_configuration_revision (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_carrier_configuration_revision (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

/**
 * mm_modem_get_hardware_revision:
 * @self: A #MMModem.
 *
 * Gets the equipment hardware revision, as reported by this #MMModem.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_hardware_revision() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment hardware revision, or %NULL if none
 * available. Do not free the returned value, it belongs to @self.
 *
 * Since: 1.8
 */
const gchar *
mm_modem_get_hardware_revision (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_hardware_revision (MM_GDBUS_MODEM (self)));
}

/**
 * mm_modem_dup_hardware_revision:
 * @self: A #MMModem.
 *
 * Gets a copy of the equipment hardware revision, as reported by this #MMModem.
 *
 * Returns: (transfer full): The equipment hardware revision, or %NULL if none
 * available. The returned value should be freed with g_free().
 *
 * Since: 1.8
 */
gchar *
mm_modem_dup_hardware_revision (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_hardware_revision (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

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
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_device_identifier() if on another thread.</warning>
 *
 * Returns: (transfer none): The device identifier, or %NULL if none available.
 * Do not free the returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_get_device_identifier (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_device_identifier (MM_GDBUS_MODEM (self)));
}

/**
 * mm_modem_dup_device_identifier:
 * @self: A #MMModem.
 *
 * Gets a copy of a best-effort device identifier based on various device
 * information like model name, firmware revision, USB/PCI/PCMCIA IDs, and other
 * properties.
 *
 * This ID is not guaranteed to be unique and may be shared between
 * identical devices with the same firmware, but is intended to be "unique
 * enough" for use as a casual device identifier for various user
 * experience operations.
 *
 * This is not the device's IMEI or ESN since those may not be available
 * before unlocking the device via a PIN.
 *
 * Returns: (transfer full): The device identifier, or %NULL if none available.
 * The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_dup_device_identifier (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_device_identifier (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

/**
 * mm_modem_get_device:
 * @self: A #MMModem.
 *
 * Gets the physical modem device reference (ie, USB, PCI, PCMCIA device), which
 * may be dependent upon the operating system.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_device() if on another thread.</warning>
 *
 * Returns: (transfer none): The device, or %NULL if none available. Do not free
 * the returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_get_device (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_device (MM_GDBUS_MODEM (self)));
}

/**
 * mm_modem_dup_device:
 * @self: A #MMModem.
 *
 * Gets a copy of the physical modem device reference (ie, USB, PCI, PCMCIA
 * device), which may be dependent upon the operating system.
 *
 * Returns: (transfer full): The device, or %NULL if none available. The
 * returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_dup_device (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_device (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

/**
 * mm_modem_get_drivers:
 * @self: A #MMModem.
 *
 * Gets the Operating System device drivers handling communication with the
 * modem hardware.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_drivers() if on another thread.</warning>
 *
 * Returns: (transfer none): The drivers, or %NULL if none available. Do not
 * free the returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar * const *
mm_modem_get_drivers (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    return mm_gdbus_modem_get_drivers (MM_GDBUS_MODEM (self));
}

/**
 * mm_modem_dup_drivers:
 * @self: A #MMModem.
 *
 * Gets a copy of the Operating System device driver handling communication with
 * the modem hardware.
 *
 * Returns: (transfer full): The drivers, or %NULL if none available. The
 * returned value should be freed with g_strfreev().
 *
 * Since: 1.0
 */
gchar **
mm_modem_dup_drivers (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    return mm_gdbus_modem_dup_drivers (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

/**
 * mm_modem_get_plugin:
 * @self: A #MMModem.
 *
 * Gets the name of the plugin handling this #MMModem.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_plugin() if on another thread.</warning>
 *
 * Returns: (transfer none): The name of the plugin, or %NULL if none
 *available. Do not free the returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_get_plugin (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_plugin (MM_GDBUS_MODEM (self)));
}

/**
 * mm_modem_dup_plugin:
 * @self: A #MMModem.
 *
 * Gets a copy of the name of the plugin handling this #MMModem.
 *
 * Returns: (transfer full): The name of the plugin, or %NULL if none available.
 * The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_dup_plugin (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_plugin (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

/**
 * mm_modem_get_primary_port:
 * @self: A #MMModem.
 *
 * Gets the name of the primary port controlling this #MMModem.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_primary_port() if on another thread.</warning>
 *
 * Returns: (transfer none): The name of the primary port. Do not free the
 * returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_get_primary_port (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_primary_port (MM_GDBUS_MODEM (self)));
}

/**
 * mm_modem_dup_primary_port:
 * @self: A #MMModem.
 *
 * Gets a copy of the name of the primary port controlling this #MMModem.
 *
 * Returns: (transfer full): The name of the primary port. The returned value
 * should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_dup_primary_port (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_primary_port (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

static void
ports_updated (MMModem *self,
               GParamSpec *pspec)
{
    g_mutex_lock (&self->priv->ports_mutex);
    {
        GVariant *dictionary;

        if (self->priv->ports)
            g_array_unref (self->priv->ports);

        dictionary = mm_gdbus_modem_get_ports (MM_GDBUS_MODEM (self));
        self->priv->ports = (dictionary ?
                             mm_common_ports_variant_to_garray (dictionary) :
                             NULL);
    }
    g_mutex_unlock (&self->priv->ports_mutex);
}

static gboolean
ensure_internal_ports (MMModem *self,
                       MMModemPortInfo **dup_ports,
                       guint *dup_ports_n)
{
    gboolean ret;
    guint i;

    g_mutex_lock (&self->priv->ports_mutex);
    {
        /* If this is the first time ever asking for the array, setup the
         * update listener and the initial array, if any. */
        if (!self->priv->ports_id) {
            GVariant *dictionary;

            dictionary = mm_gdbus_modem_dup_ports (MM_GDBUS_MODEM (self));
            if (dictionary) {
                self->priv->ports = mm_common_ports_variant_to_garray (dictionary);
                g_variant_unref (dictionary);
            }

            /* No need to clear this signal connection when freeing self */
            self->priv->ports_id =
                g_signal_connect (self,
                                  "notify::ports",
                                  G_CALLBACK (ports_updated),
                                  NULL);
        }

        if (!self->priv->ports)
            ret = FALSE;
        else {
            ret = TRUE;

            if (dup_ports && dup_ports_n) {
                *dup_ports_n = self->priv->ports->len;
                if (self->priv->ports->len > 0) {
                    *dup_ports = g_malloc (sizeof (MMModemPortInfo) * self->priv->ports->len);

                    /* Deep-copy the array */
                    for (i = 0; i < self->priv->ports->len; i++) {
                        MMModemPortInfo *dst = &(*dup_ports)[i];
                        MMModemPortInfo *src = &g_array_index (self->priv->ports, MMModemPortInfo, i);

                        dst->name = g_strdup (src->name);
                        dst->type = src->type;
                    }
                } else
                    *dup_ports = NULL;
            }
        }
    }
    g_mutex_unlock (&self->priv->ports_mutex);

    return ret;
}

/**
 * mm_modem_peek_ports:
 * @self: A #MMModem.
 * @ports: (out) (array length=n_ports) (transfer none): Return location for the
 *  array of #MMModemPortInfo values. Do not free the returned value, it is
 *  owned by @self.
 * @n_ports: (out): Return location for the number of values in @ports.
 *
 * Gets the list of ports in the modem.
 *
 * Returns: %TRUE if @ports and @n_ports are set, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_modem_peek_ports (MMModem *self,
                     const MMModemPortInfo **ports,
                     guint *n_ports)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (ports != NULL, FALSE);
    g_return_val_if_fail (n_ports != NULL, FALSE);

    if (!ensure_internal_ports (self, NULL, NULL))
        return FALSE;

    *n_ports = self->priv->ports->len;
    *ports = (MMModemPortInfo *)self->priv->ports->data;
    return TRUE;
}

/**
 * mm_modem_get_ports:
 * @self: A #MMModem.
 * @ports: (out) (array length=n_ports): Return location for the array of
 *  #MMModemPortInfo values. The returned array should be freed with
 *  mm_modem_port_info_array_free() when no longer needed.
 * @n_ports: (out): Return location for the number of values in @ports.
 *
 * Gets the list of ports in the modem.
 *
 * Returns: %TRUE if @ports and @n_ports are set, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_modem_get_ports (MMModem *self,
                    MMModemPortInfo **ports,
                    guint *n_ports)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (ports != NULL, FALSE);
    g_return_val_if_fail (n_ports != NULL, FALSE);

    return ensure_internal_ports (self, ports, n_ports);
}

/*****************************************************************************/

/**
 * mm_modem_get_equipment_identifier:
 * @self: A #MMModem.
 *
 * Gets the identity of the #MMModem.
 *
 * This will be the IMEI number for GSM devices and the hex-format ESN/MEID
 * for CDMA devices.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_equipment_identifier() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment identifier, or %NULL if none
 * available. Do not free the returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_get_equipment_identifier (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_equipment_identifier (MM_GDBUS_MODEM (self)));
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
 * Returns: (transfer full): The equipment identifier, or %NULL if none
 * available. The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_dup_equipment_identifier (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_equipment_identifier (MM_GDBUS_MODEM (self)));
}

/*****************************************************************************/

/**
 * mm_modem_get_own_numbers: (skip)
 * @self: A #MMModem.
 *
 * Gets the list of numbers (e.g. MSISDN in 3GPP) being currently handled by
 * this modem.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_dup_own_numbers() if on another thread.</warning>
 *
 * Returns: (transfer none): The list of own numbers or %NULL if none available.
 * Do not free the returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar *const *
mm_modem_get_own_numbers (MMModem *self)
{
    const gchar *const *own;

    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    own = mm_gdbus_modem_get_own_numbers (MM_GDBUS_MODEM (self));

    return (own && own[0] ? own : NULL);
}

/**
 * mm_modem_dup_own_numbers:
 * @self: A #MMModem.
 *
 * Gets a copy of the list of numbers (e.g. MSISDN in 3GPP) being currently
 * handled by this modem.
 *
 * Returns: (transfer full): The list of own numbers or %NULL if none is
 * available. The returned value should be freed with g_strfreev().
 *
 * Since: 1.0
 */
gchar **
mm_modem_dup_own_numbers (MMModem *self)
{
    gchar **own;

    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    own = mm_gdbus_modem_dup_own_numbers (MM_GDBUS_MODEM (self));
    if (own && own[0])
        return own;

    g_strfreev (own);
    return NULL;
}

/*****************************************************************************/

/**
 * mm_modem_get_unlock_required:
 * @self: A #MMModem.
 *
 * Gets current lock state of the #MMModem.
 *
 * Returns: A #MMModemLock value, specifying the current lock state.
 *
 * Since: 1.0
 */
MMModemLock
mm_modem_get_unlock_required (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), MM_MODEM_LOCK_UNKNOWN);

    return (MMModemLock) mm_gdbus_modem_get_unlock_required (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

static void
unlock_retries_updated (MMModem *self,
                        GParamSpec *pspec)
{
    g_mutex_lock (&self->priv->unlock_retries_mutex);
    {
        GVariant *dictionary;

        g_clear_object (&self->priv->unlock_retries);

        /* TODO: update existing object instead of re-creating? */
        dictionary = mm_gdbus_modem_get_unlock_retries (MM_GDBUS_MODEM (self));
        if (dictionary)
            self->priv->unlock_retries = mm_unlock_retries_new_from_dictionary (dictionary);
    }
    g_mutex_unlock (&self->priv->unlock_retries_mutex);
}

static void
ensure_internal_unlock_retries (MMModem *self,
                                MMUnlockRetries **dup)
{
    g_mutex_lock (&self->priv->unlock_retries_mutex);
    {
        /* If this is the first time ever asking for the object, setup the
         * update listener and the initial object, if any. */
        if (!self->priv->unlock_retries_id) {
            GVariant *dictionary;

            dictionary = mm_gdbus_modem_dup_unlock_retries (MM_GDBUS_MODEM (self));
            if (dictionary) {
                self->priv->unlock_retries = mm_unlock_retries_new_from_dictionary (dictionary);
                g_variant_unref (dictionary);
            }

            /* No need to clear this signal connection when freeing self */
            self->priv->unlock_retries_id =
                g_signal_connect (self,
                                  "notify::unlock-retries",
                                  G_CALLBACK (unlock_retries_updated),
                                  NULL);
        }

        if (dup && self->priv->unlock_retries)
            *dup = g_object_ref (self->priv->unlock_retries);
    }
    g_mutex_unlock (&self->priv->unlock_retries_mutex);
}

/**
 * mm_modem_get_unlock_retries:
 * @self: A #MMModem.
 *
 * Gets a #MMUnlockRetries object, which provides, for each
 * <link linkend="MMModemLock">MMModemLock</link> handled by the modem, the
 * number of PIN tries remaining before the code becomes blocked (requiring a
 * PUK) or permanently blocked.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_get_unlock_retries() again to get a new #MMUnlockRetries with the
 * new values.</warning>
 *
 * Returns: (transfer full): A #MMUnlockRetries that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.0
 */
MMUnlockRetries *
mm_modem_get_unlock_retries (MMModem *self)
{
    MMUnlockRetries *unlock_retries = NULL;

    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    ensure_internal_unlock_retries (self, &unlock_retries);
    return unlock_retries;
}

/**
 * mm_modem_peek_unlock_retries:
 * @self: A #MMModem.
 *
 * Gets a #MMUnlockRetries object, which provides, for each
 * <link linkend="MMModemLock">MMModemLock</link> handled by the modem, the
 * number of PIN tries remaining before the code becomes blocked (requiring a
 * PUK) or permanently blocked.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_get_unlock_retries() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMUnlockRetries. Do not free the returned value,
 * it belongs to @self.
 *
 * Since: 1.0
 */
MMUnlockRetries *
mm_modem_peek_unlock_retries (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    ensure_internal_unlock_retries (self, NULL);
    return self->priv->unlock_retries;
}

/*****************************************************************************/

/**
 * mm_modem_get_state:
 * @self: A #MMModem.
 *
 * Gets the overall state of the #MMModem.
 *
 * Returns: A #MMModemState value.
 *
 * Since: 1.0
 */
MMModemState
mm_modem_get_state (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), MM_MODEM_STATE_UNKNOWN);

    return (MMModemState) mm_gdbus_modem_get_state (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

/**
 * mm_modem_get_state_failed_reason:
 * @self: A #MMModem.
 *
 * Gets the reason specifying why the modem is in #MM_MODEM_STATE_FAILED state.
 *
 * Returns: A #MMModemStateFailedReason value.
 *
 * Since: 1.0
 */
MMModemStateFailedReason
mm_modem_get_state_failed_reason (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), MM_MODEM_STATE_FAILED_REASON_UNKNOWN);

    return (MMModemStateFailedReason) mm_gdbus_modem_get_state_failed_reason (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

/**
 * mm_modem_get_power_state:
 * @self: A #MMModem.
 *
 * Gets the power state of the #MMModem.
 *
 * Returns: A #MMModemPowerState value.
 *
 * Since: 1.0
 */
MMModemPowerState
mm_modem_get_power_state (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), MM_MODEM_POWER_STATE_UNKNOWN);

    return (MMModemPowerState) mm_gdbus_modem_get_power_state (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

/**
 * mm_modem_get_access_technologies:
 * @self: A #MMModem.
 *
 * Gets the current network access technology used by the #MMModem to
 * communicate with the network.
 *
 * Returns: A ##MMModemAccessTechnology value.
 *
 * Since: 1.0
 */
MMModemAccessTechnology
mm_modem_get_access_technologies (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);

    return (MMModemAccessTechnology) mm_gdbus_modem_get_access_technologies (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

/**
 * mm_modem_get_signal_quality:
 * @self: A #MMModem.
 * @recent: (out): Return location for the flag specifying if the signal quality
 *  value was recent or not.
 *
 * Gets the signal quality value in percent (0 - 100) of the dominant access
 * technology the #MMModem is using to communicate with the network.
 *
 * Always 0 for POTS devices.
 *
 * Returns: The signal quality.
 *
 * Since: 1.0
 */
guint
mm_modem_get_signal_quality (MMModem *self,
                             gboolean *recent)
{
    GVariant *variant;
    gboolean is_recent = FALSE;
    guint quality = 0;

    g_return_val_if_fail (MM_IS_MODEM (self), 0);

    variant = mm_gdbus_modem_dup_signal_quality (MM_GDBUS_MODEM (self));
    if (variant) {
        g_variant_get (variant,
                       "(ub)",
                       &quality,
                       &is_recent);
        g_variant_unref (variant);
    }

    if (recent)
        *recent = is_recent;
    return quality;
}

/*****************************************************************************/

static void
supported_modes_updated (MMModem *self,
                         GParamSpec *pspec)
{
    g_mutex_lock (&self->priv->supported_modes_mutex);
    {
        GVariant *dictionary;

        if (self->priv->supported_modes)
            g_array_unref (self->priv->supported_modes);

        dictionary = mm_gdbus_modem_get_supported_modes (MM_GDBUS_MODEM (self));
        self->priv->supported_modes = (dictionary ?
                                       mm_common_mode_combinations_variant_to_garray (dictionary) :
                                       NULL);
    }
    g_mutex_unlock (&self->priv->supported_modes_mutex);
}

static gboolean
ensure_internal_supported_modes (MMModem *self,
                                 MMModemModeCombination **dup_modes,
                                 guint *dup_modes_n)
{
    gboolean ret;

    g_mutex_lock (&self->priv->supported_modes_mutex);
    {
        /* If this is the first time ever asking for the array, setup the
         * update listener and the initial array, if any. */
        if (!self->priv->supported_modes_id) {
            GVariant *dictionary;

            dictionary = mm_gdbus_modem_dup_supported_modes (MM_GDBUS_MODEM (self));
            if (dictionary) {
                self->priv->supported_modes = mm_common_mode_combinations_variant_to_garray (dictionary);
                g_variant_unref (dictionary);
            }

            /* No need to clear this signal connection when freeing self */
            self->priv->supported_modes_id =
                g_signal_connect (self,
                                  "notify::supported-modes",
                                  G_CALLBACK (supported_modes_updated),
                                  NULL);
        }

        if (!self->priv->supported_modes)
            ret = FALSE;
        else {
            ret = TRUE;

            if (dup_modes && dup_modes_n) {
                *dup_modes_n = self->priv->supported_modes->len;
                if (self->priv->supported_modes->len > 0) {
                    *dup_modes = g_malloc (sizeof (MMModemModeCombination) * self->priv->supported_modes->len);
                    memcpy (*dup_modes, self->priv->supported_modes->data, sizeof (MMModemModeCombination) * self->priv->supported_modes->len);
                } else
                    *dup_modes = NULL;
            }
        }
    }
    g_mutex_unlock (&self->priv->supported_modes_mutex);

    return ret;
}

/**
 * mm_modem_get_supported_modes:
 * @self: A #MMModem.
 * @modes: (out) (array length=n_modes): Return location for the array of
 *  #MMModemModeCombination structs. The returned array should be freed with
 *  g_free() when no longer needed.
 * @n_modes: (out): Return location for the number of values in @modes.
 *
 * Gets the list of supported mode combinations.
 *
 * Returns: %TRUE if @modes and @n_modes are set, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_modem_get_supported_modes (MMModem *self,
                              MMModemModeCombination **modes,
                              guint *n_modes)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (modes != NULL, FALSE);
    g_return_val_if_fail (n_modes != NULL, FALSE);

    return ensure_internal_supported_modes (self, modes, n_modes);
}

/**
 * mm_modem_peek_supported_modes:
 * @self: A #MMModem.
 * @modes: (out) (array length=n_modes): Return location for the array of
 *  #MMModemModeCombination values. Do not free the returned array, it is owned
 *  by @self.
 * @n_modes: (out): Return location for the number of values in @modes.
 *
 * Gets the list of supported mode combinations.
 *
 * Returns: %TRUE if @modes and @n_modes are set, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_modem_peek_supported_modes (MMModem *self,
                               const MMModemModeCombination **modes,
                               guint *n_modes)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (modes != NULL, FALSE);
    g_return_val_if_fail (n_modes != NULL, FALSE);

    if (!ensure_internal_supported_modes (self, NULL, NULL))
        return FALSE;

    *n_modes = self->priv->supported_modes->len;
    *modes = (MMModemModeCombination *)self->priv->supported_modes->data;
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_modem_get_current_modes:
 * @self: A #MMModem.
 * @allowed: (out): Return location for a bitmask of #MMModemMode values.
 * @preferred: (out): Return location for a #MMModemMode value.
 *
 * Gets the list of modes specifying the access technologies (eg 2G/3G/4G)
 * the #MMModem is currently allowed to use when connecting to a network, as
 * well as the preferred one, if any.
 *
 * Returns: %TRUE if @allowed and @preferred are set, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_modem_get_current_modes (MMModem *self,
                            MMModemMode *allowed,
                            MMModemMode *preferred)
{
    GVariant *variant;

    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (allowed != NULL, FALSE);
    g_return_val_if_fail (preferred != NULL, FALSE);

    variant = mm_gdbus_modem_dup_current_modes (MM_GDBUS_MODEM (self));
    if (variant) {
        g_variant_get (variant,
                       "(uu)",
                       allowed,
                       preferred);
        g_variant_unref (variant);
        return TRUE;
    }

    return FALSE;
}

/*****************************************************************************/

static void
supported_bands_updated (MMModem *self,
                         GParamSpec *pspec)
{
    g_mutex_lock (&self->priv->supported_bands_mutex);
    {
        GVariant *dictionary;

        if (self->priv->supported_bands)
            g_array_unref (self->priv->supported_bands);

        dictionary = mm_gdbus_modem_get_supported_bands (MM_GDBUS_MODEM (self));
        self->priv->supported_bands = (dictionary ?
                                       mm_common_bands_variant_to_garray (dictionary) :
                                       NULL);
    }
    g_mutex_unlock (&self->priv->supported_bands_mutex);
}

static gboolean
ensure_internal_supported_bands (MMModem *self,
                                 MMModemBand **dup_bands,
                                 guint *dup_bands_n)
{
    gboolean ret;

    g_mutex_lock (&self->priv->supported_bands_mutex);
    {
        /* If this is the first time ever asking for the array, setup the
         * update listener and the initial array, if any. */
        if (!self->priv->supported_bands_id) {
            GVariant *dictionary;

            dictionary = mm_gdbus_modem_dup_supported_bands (MM_GDBUS_MODEM (self));
            if (dictionary) {
                self->priv->supported_bands = mm_common_bands_variant_to_garray (dictionary);
                g_variant_unref (dictionary);
            }

            /* No need to clear this signal connection when freeing self */
            self->priv->supported_bands_id =
                g_signal_connect (self,
                                  "notify::supported-bands",
                                  G_CALLBACK (supported_bands_updated),
                                  NULL);
        }

        if (!self->priv->supported_bands)
            ret = FALSE;
        else {
            ret = TRUE;

            if (dup_bands && dup_bands_n) {
                *dup_bands_n = self->priv->supported_bands->len;
                if (self->priv->supported_bands->len > 0) {
                    *dup_bands = g_malloc (sizeof (MMModemBand) * self->priv->supported_bands->len);
                    memcpy (*dup_bands, self->priv->supported_bands->data, sizeof (MMModemBand) * self->priv->supported_bands->len);
                } else
                    *dup_bands = NULL;
            }
        }
    }
    g_mutex_unlock (&self->priv->supported_bands_mutex);

    return ret;
}

/**
 * mm_modem_get_supported_bands:
 * @self: A #MMModem.
 * @bands: (out) (array length=n_bands): Return location for the array of
 *  #MMModemBand values. The returned array should be freed with g_free() when
 *  no longer needed.
 * @n_bands: (out): Return location for the number of values in @bands.
 *
 * Gets the list of radio frequency and technology bands supported by the
 * #MMModem.
 *
 * For POTS devices, only #MM_MODEM_BAND_ANY will be returned in @bands.
 *
 * Returns: %TRUE if @bands and @n_bands are set, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_modem_get_supported_bands (MMModem *self,
                              MMModemBand **bands,
                              guint *n_bands)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (bands != NULL, FALSE);
    g_return_val_if_fail (n_bands != NULL, FALSE);

    return ensure_internal_supported_bands (self, bands, n_bands);
}

/**
 * mm_modem_peek_supported_bands:
 * @self: A #MMModem.
 * @bands: (out) (array length=n_bands): Return location for the array of
 *  #MMModemBand values. Do not free the returned array, it is owned by @self.
 * @n_bands: (out): Return location for the number of values in @bands.
 *
 * Gets the list of radio frequency and technology bands supported by the
 * #MMModem.
 *
 * For POTS devices, only #MM_MODEM_BAND_ANY will be returned in @bands.
 *
 * Returns: %TRUE if @bands and @n_bands are set, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_modem_peek_supported_bands (MMModem *self,
                               const MMModemBand **bands,
                               guint *n_bands)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (bands != NULL, FALSE);
    g_return_val_if_fail (n_bands != NULL, FALSE);

    if (!ensure_internal_supported_bands (self, NULL, NULL))
        return FALSE;

    *n_bands = self->priv->supported_bands->len;
    *bands = (MMModemBand *)self->priv->supported_bands->data;
    return TRUE;
}

/*****************************************************************************/

static void
current_bands_updated (MMModem *self,
                       GParamSpec *pspec)
{
    g_mutex_lock (&self->priv->current_bands_mutex);
    {
        GVariant *dictionary;

        if (self->priv->current_bands)
            g_array_unref (self->priv->current_bands);

        dictionary = mm_gdbus_modem_get_current_bands (MM_GDBUS_MODEM (self));
        self->priv->current_bands = (dictionary ?
                                     mm_common_bands_variant_to_garray (dictionary) :
                                     NULL);
    }
    g_mutex_unlock (&self->priv->current_bands_mutex);
}

static gboolean
ensure_internal_current_bands (MMModem *self,
                               MMModemBand **dup_bands,
                               guint *dup_bands_n)
{
    gboolean ret;

    g_mutex_lock (&self->priv->current_bands_mutex);
    {
        /* If this is the first time ever asking for the array, setup the
         * update listener and the initial array, if any. */
        if (!self->priv->current_bands_id) {
            GVariant *dictionary;

            dictionary = mm_gdbus_modem_dup_current_bands (MM_GDBUS_MODEM (self));
            if (dictionary) {
                self->priv->current_bands = mm_common_bands_variant_to_garray (dictionary);
                g_variant_unref (dictionary);
            }

            /* No need to clear this signal connection when freeing self */
            self->priv->current_bands_id =
                g_signal_connect (self,
                                  "notify::current-bands",
                                  G_CALLBACK (current_bands_updated),
                                  NULL);
        }

        if (!self->priv->current_bands)
            ret = FALSE;
        else {
            ret = TRUE;

            if (dup_bands && dup_bands_n) {
                *dup_bands_n = self->priv->current_bands->len;
                if (self->priv->current_bands->len > 0) {
                    *dup_bands = g_malloc (sizeof (MMModemBand) * self->priv->current_bands->len);
                    memcpy (*dup_bands, self->priv->current_bands->data, sizeof (MMModemBand) * self->priv->current_bands->len);
                } else
                    *dup_bands = NULL;
            }
        }
    }
    g_mutex_unlock (&self->priv->current_bands_mutex);

    return ret;
}

/**
 * mm_modem_get_current_bands:
 * @self: A #MMModem.
 * @bands: (out) (array length=n_bands): Return location for the array of
 *  #MMModemBand values. The returned array should be freed with g_free() when
 *  no longer needed.
 * @n_bands: (out): Return location for the number of values in @bands.
 *
 * Gets the list of radio frequency and technology bands the #MMModem is
 * currently using when connecting to a network.
 *
 * For POTS devices, only the #MM_MODEM_BAND_ANY band is supported.
 *
 * Returns: %TRUE if @bands and @n_bands are set, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_modem_get_current_bands (MMModem *self,
                            MMModemBand **bands,
                            guint *n_bands)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (bands != NULL, FALSE);
    g_return_val_if_fail (n_bands != NULL, FALSE);

    return ensure_internal_current_bands (self, bands, n_bands);
}

/**
 * mm_modem_peek_current_bands:
 * @self: A #MMModem.
 * @bands: (out) (array length=n_bands): Return location for the array of
 *  #MMModemBand values. Do not free the returned value, it is owned by @self.
 * @n_bands: (out): Return location for the number of values in @bands.
 *
 * Gets the list of radio frequency and technology bands the #MMModem is
 * currently using when connecting to a network.
 *
 * For POTS devices, only the #MM_MODEM_BAND_ANY band is supported.
 *
 * Returns: %TRUE if @bands and @n_bands are set, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_modem_peek_current_bands (MMModem *self,
                             const MMModemBand **bands,
                             guint *n_bands)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (bands != NULL, FALSE);
    g_return_val_if_fail (n_bands != NULL, FALSE);

    if (!ensure_internal_current_bands (self, NULL, NULL))
        return FALSE;

    *n_bands = self->priv->current_bands->len;
    *bands = (MMModemBand *)self->priv->current_bands->data;
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_modem_get_supported_ip_families:
 * @self: A #MMModem.
 *
 * Gets the list of supported IP families.
 *
 * Returns: A bitmask of #MMBearerIpFamily values.
 *
 * Since: 1.0
 */
MMBearerIpFamily
mm_modem_get_supported_ip_families (MMModem *self)
{
    g_return_val_if_fail (MM_IS_MODEM (self), MM_BEARER_IP_FAMILY_NONE);

    return (MMBearerIpFamily) mm_gdbus_modem_get_supported_ip_families (MM_GDBUS_MODEM (self));
}

/*****************************************************************************/

/**
 * mm_modem_enable_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_enable().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_enable().
 *
 * Returns: %TRUE if the modem was properly enabled, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_enable_finish (MMModem *self,
                        GAsyncResult *res,
                        GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_enable_finish (MM_GDBUS_MODEM (self), res, error);
}

/**
 * mm_modem_enable:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously tries to enable the #MMModem. When enabled, the modem's radio
 * is powered on and data sessions, voice calls, location services, and Short
 * Message Service may be available.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_enable_finish() to get the result of the operation.
 *
 * See mm_modem_enable_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
 */
void
mm_modem_enable (MMModem *self,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));

    mm_gdbus_modem_call_enable (MM_GDBUS_MODEM (self), TRUE, cancellable, callback, user_data);
}

/**
 * mm_modem_enable_sync:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously tries to enable the #MMModem. When enabled, the modem's radio
 * is powered on and data sessions, voice calls, location services, and Short
 * Message Service may be available.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_enable() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the modem was properly enabled, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_enable_sync (MMModem *self,
                      GCancellable *cancellable,
                      GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_enable_sync (MM_GDBUS_MODEM (self), TRUE, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_disable_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_disable().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_disable().
 *
 * Returns: %TRUE if the modem was properly disabled, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_disable_finish (MMModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_enable_finish (MM_GDBUS_MODEM (self), res, error);
}

/**
 * mm_modem_disable:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously tries to disable the #MMModem. When disabled, the modem enters
 * low-power state and no network-related operations are available.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_disable_finish() to get the result of the operation.
 *
 * See mm_modem_disable_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
 */
void
mm_modem_disable (MMModem *self,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));

    mm_gdbus_modem_call_enable (MM_GDBUS_MODEM (self), FALSE, cancellable, callback, user_data);
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
 * The calling thread is blocked until a reply is received. See
 * mm_modem_disable() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the modem was properly disabled, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_disable_sync (MMModem *self,
                      GCancellable *cancellable,
                      GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_enable_sync (MM_GDBUS_MODEM (self), FALSE, cancellable, error);
}

/*****************************************************************************/

typedef struct {
    gchar **bearer_paths;
    GList *bearer_objects;
    guint i;
} ListBearersContext;

static void
bearer_object_list_free (GList *list)
{
    g_list_free_full (list, g_object_unref);
}

static void
list_bearers_context_free (ListBearersContext *ctx)
{
    g_strfreev (ctx->bearer_paths);
    bearer_object_list_free (ctx->bearer_objects);
    g_slice_free (ListBearersContext, ctx);
}

/**
 * mm_modem_list_bearers_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_list_bearers().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_list_bearers().
 *
 * Returns: (transfer full) (element-type ModemManager.Bearer): The list of
 * #MMBearer objects, or %NULL if either none found or if @error is set.
 *
 * Since: 1.0
 */
GList *
mm_modem_list_bearers_finish (MMModem *self,
                              GAsyncResult *res,
                              GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    return g_task_propagate_pointer (G_TASK (res), error);
}

static void create_next_bearer (GTask *task);

static void
modem_list_bearers_build_object_ready (GDBusConnection *connection,
                                       GAsyncResult *res,
                                       GTask *task)
{
    GObject *bearer;
    GError *error = NULL;
    GObject *source_object;
    ListBearersContext *ctx;

    source_object = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, &error);
    g_object_unref (source_object);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Keep the object */
    ctx->bearer_objects = g_list_prepend (ctx->bearer_objects, bearer);

    /* If no more bearers, just end here. */
    if (!ctx->bearer_paths[++ctx->i]) {
        GList *bearer_objects;

        bearer_objects = g_list_copy_deep (ctx->bearer_objects,
                                           (GCopyFunc)g_object_ref,
                                           NULL);
        g_task_return_pointer (task,
                               bearer_objects,
                               (GDestroyNotify)bearer_object_list_free);
        g_object_unref (task);
        return;
    }

    /* Keep on creating next object */
    create_next_bearer (task);
}

static void
create_next_bearer (GTask *task)
{
    MMModem *self;
    ListBearersContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    g_async_initable_new_async (MM_TYPE_BEARER,
                                G_PRIORITY_DEFAULT,
                                g_task_get_cancellable (task),
                                (GAsyncReadyCallback)modem_list_bearers_build_object_ready,
                                task,
                                "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                "g-name",           MM_DBUS_SERVICE,
                                "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                "g-object-path",    ctx->bearer_paths[ctx->i],
                                "g-interface-name", "org.freedesktop.ModemManager1.Bearer",
                                NULL);
}

/**
 * mm_modem_list_bearers:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously lists the packet data bearers in the #MMModem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_list_bearers_finish() to get the result of the operation.
 *
 * See mm_modem_list_bearers_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
 */
void
mm_modem_list_bearers (MMModem *self,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    ListBearersContext *ctx;
    GTask *task;

    g_return_if_fail (MM_IS_MODEM (self));

    ctx = g_slice_new0 (ListBearersContext);

    /* Read from the property, skip List() */
    ctx->bearer_paths = mm_gdbus_modem_dup_bearers (MM_GDBUS_MODEM (self));

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)list_bearers_context_free);

    /* If no bearers, just end here. */
    if (!ctx->bearer_paths || !ctx->bearer_paths[0]) {
        g_task_return_pointer (task, NULL, NULL);
        g_object_unref (task);
        return;
    }

    /* Got list of paths. If at least one found, start creating objects for each */
    ctx->i = 0;
    create_next_bearer (task);
}

/**
 * mm_modem_list_bearers_sync:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously lists the packet data bearers in the #MMModem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_list_bearers() for the asynchronous version of this method.
 *
 * Returns: (transfer full) (element-type ModemManager.Bearer): The list of
 * #MMBearer objects, or %NULL if either none found or if @error is set.
 *
 * Since: 1.0
 */
GList *
mm_modem_list_bearers_sync (MMModem *self,
                            GCancellable *cancellable,
                            GError **error)
{
    GList *bearer_objects = NULL;
    gchar **bearer_paths = NULL;
    guint i;

    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    /* Read from the property, skip List() */
    bearer_paths = mm_gdbus_modem_dup_bearers (MM_GDBUS_MODEM (self));

    /* Only non-empty lists are returned */
    if (!bearer_paths)
        return NULL;

    for (i = 0; bearer_paths[i]; i++) {
        GObject *bearer;

        bearer = g_initable_new (MM_TYPE_BEARER,
                                 cancellable,
                                 error,
                                 "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                 "g-name",           MM_DBUS_SERVICE,
                                 "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                 "g-object-path",    bearer_paths[i],
                                 "g-interface-name", "org.freedesktop.ModemManager1.Bearer",
                                 NULL);
        if (!bearer) {
            bearer_object_list_free (bearer_objects);
            g_strfreev (bearer_paths);
            return NULL;
        }

        /* Keep the object */
        bearer_objects = g_list_prepend (bearer_objects, bearer);
    }

    g_strfreev (bearer_paths);
    return bearer_objects;
}

/*****************************************************************************/

/**
 * mm_modem_create_bearer_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_create_bearer().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_create_bearer().
 *
 * Returns: (transfer full): A newly created #MMBearer, or %NULL if @error is
 * set.
 *
 * Since: 1.0
 */
MMBearer *
mm_modem_create_bearer_finish (MMModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_new_bearer_ready (GDBusConnection *connection,
                        GAsyncResult *res,
                        GTask *task)
{
    GError *error = NULL;
    GObject *bearer;
    GObject *source_object;

    source_object = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, &error);
    g_object_unref (source_object);

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);

    g_object_unref (task);
}

static void
modem_create_bearer_ready (MMModem *self,
                           GAsyncResult *res,
                           GTask *task)
{
    GError *error = NULL;
    gchar *bearer_path = NULL;

    if (!mm_gdbus_modem_call_create_bearer_finish (MM_GDBUS_MODEM (self),
                                                   &bearer_path,
                                                   res,
                                                   &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        g_free (bearer_path);
        return;
    }

    g_async_initable_new_async (MM_TYPE_BEARER,
                                G_PRIORITY_DEFAULT,
                                g_task_get_cancellable (task),
                                (GAsyncReadyCallback)modem_new_bearer_ready,
                                task,
                                "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                "g-name",           MM_DBUS_SERVICE,
                                "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                "g-object-path",    bearer_path,
                                "g-interface-name", "org.freedesktop.ModemManager1.Bearer",
                                NULL);
    g_free (bearer_path);
}

/**
 * mm_modem_create_bearer:
 * @self: A #MMModem.
 * @properties: A #MMBearerProperties object with the properties to use.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously creates a new packet data bearer in the #MMModem.
 *
 * This request may fail if the modem does not support additional bearers,
 * if too many bearers are already defined, or if @properties are invalid.
 *
 * See <link linkend="gdbus-method-org-freedesktop-ModemManager1-Modem.CreateBearer">CreateBearer</link>
 * to check which properties may be passed.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_create_bearer_finish() to get the result of the operation.
 *
 * See mm_modem_create_bearer_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
 */
void
mm_modem_create_bearer (MMModem *self,
                        MMBearerProperties *properties,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GTask *task;
    GVariant *dictionary;

    g_return_if_fail (MM_IS_MODEM (self));

    task = g_task_new (self, cancellable, callback, user_data);

    dictionary = mm_bearer_properties_get_dictionary (properties);

    mm_gdbus_modem_call_create_bearer (
        MM_GDBUS_MODEM (self),
        dictionary,
        cancellable,
        (GAsyncReadyCallback)modem_create_bearer_ready,
        task);

    g_variant_unref (dictionary);
}

/**
 * mm_modem_create_bearer_sync:
 * @self: A #MMModem.
 * @properties: A #MMBearerProperties object with the properties to use.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously creates a new packet data bearer in the #MMModem.
 *
 * This request may fail if the modem does not support additional bearers,
 * if too many bearers are already defined, or if @properties are invalid.
 *
 * See <link linkend="gdbus-method-org-freedesktop-ModemManager1-Modem.CreateBearer">CreateBearer</link>
 * to check which properties may be passed.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_create_bearer() for the asynchronous version of this method.
 *
 * Returns: (transfer full): A newly created #MMBearer, or %NULL if @error is
 * set.
 *
 * Since: 1.0
 */
MMBearer *
mm_modem_create_bearer_sync (MMModem *self,
                             MMBearerProperties *properties,
                             GCancellable *cancellable,
                             GError **error)
{
    GObject *bearer = NULL;
    gchar *bearer_path = NULL;
    GVariant *dictionary;

    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    dictionary = mm_bearer_properties_get_dictionary (properties);
    mm_gdbus_modem_call_create_bearer_sync (MM_GDBUS_MODEM (self),
                                            dictionary,
                                            &bearer_path,
                                            cancellable,
                                            error);
    if (bearer_path) {
        bearer = g_initable_new (MM_TYPE_BEARER,
                                 cancellable,
                                 error,
                                 "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                 "g-name",           MM_DBUS_SERVICE,
                                 "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                 "g-object-path",    bearer_path,
                                 "g-interface-name", "org.freedesktop.ModemManager1.Bearer",
                                 NULL);
        g_free (bearer_path);
    }

    g_variant_unref (dictionary);

    return (bearer ? MM_BEARER (bearer) : NULL);
}

/*****************************************************************************/

/**
 * mm_modem_delete_bearer_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_delete_bearer().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_delete_bearer().
 *
 * Returns: %TRUE if the bearer was deleted, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_delete_bearer_finish (MMModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_delete_bearer_finish (MM_GDBUS_MODEM (self), res, error);
}

/**
 * mm_modem_delete_bearer:
 * @self: A #MMModem.
 * @bearer: Path of the bearer to delete.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously deletes a given bearer from the #MMModem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_delete_bearer_finish() to get the result of the operation.
 *
 * See mm_modem_delete_bearer_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
 */
void
mm_modem_delete_bearer (MMModem *self,
                        const gchar *bearer,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));

    mm_gdbus_modem_call_delete_bearer (MM_GDBUS_MODEM (self), bearer, cancellable, callback, user_data);
}

/**
 * mm_modem_delete_bearer_sync:
 * @self: A #MMModem.
 * @bearer: Path of the bearer to delete.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.

 * Synchronously deletes a given bearer from the #MMModem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_delete_bearer() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the bearer was deleted, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_delete_bearer_sync (MMModem *self,
                             const gchar *bearer,
                             GCancellable *cancellable,
                             GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_delete_bearer_sync (MM_GDBUS_MODEM (self), bearer, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_reset_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_reset().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_reset().
 *
 * Returns: %TRUE if the reset was successful, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_reset_finish (MMModem *self,
                       GAsyncResult *res,
                       GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_reset_finish (MM_GDBUS_MODEM (self), res, error);
}

/**
 * mm_modem_reset:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously clears non-persistent configuration and state, and returns the
 * device to a newly-powered-on state.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_reset_finish() to get the result of the operation.
 *
 * See mm_modem_reset_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
 */
void
mm_modem_reset (MMModem *self,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));

    mm_gdbus_modem_call_reset (MM_GDBUS_MODEM (self), cancellable, callback, user_data);
}

/**
 * mm_modem_reset_sync:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously clears non-persistent configuration and state, and returns the
 * device to a newly-powered-on state.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_reset()
 * for the asynchronous version of this method.
 *
 * Returns: %TRUE if the reset was successful, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_reset_sync (MMModem *self,
                     GCancellable *cancellable,
                     GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_reset_sync (MM_GDBUS_MODEM (self), cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_factory_reset_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_factory_reset().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_factory_reset().
 *
 * Returns: %TRUE if the factory_reset was successful, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_factory_reset_finish (MMModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_factory_reset_finish (MM_GDBUS_MODEM (self), res, error);
}

/**
 * mm_modem_factory_reset:
 * @self: A #MMModem.
 * @code: Carrier-supplied code required to reset the modem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously clears the modem's configuration (including persistent
 * configuration and state), and returns the device to a factory-default state.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_factory_reset_finish() to get the result of the operation.
 *
 * See mm_modem_factory_reset_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
 */
void
mm_modem_factory_reset (MMModem *self,
                        const gchar *code,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));

    mm_gdbus_modem_call_factory_reset (MM_GDBUS_MODEM (self), code, cancellable, callback, user_data);
}

/**
 * mm_modem_factory_reset_sync:
 * @self: A #MMModem.
 * @code: Carrier-supplied code required to reset the modem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously clears the modem's configuration (including persistent
 * configuration and state), and returns the device to a factory-default state.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_factory_reset() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the factory reset was successful, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_factory_reset_sync (MMModem *self,
                             const gchar *code,
                             GCancellable *cancellable,
                             GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_factory_reset_sync (MM_GDBUS_MODEM (self), code, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_command_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_command().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_command().
 *
 * Returns: (transfer full): A newly allocated string with the reply to the
 * command, or #NULL if @error is set. The returned value should be freed with
 * g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_command_finish (MMModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    gchar *result;

    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    if (!mm_gdbus_modem_call_command_finish (MM_GDBUS_MODEM (self), &result, res, error))
        return NULL;

    return result;
}

/**
 * mm_modem_command:
 * @self: A #MMModem.
 * @cmd: AT command to run.
 * @timeout: Maximum time to wait for the response, in seconds.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously runs an AT command in the modem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_command_finish() to get the result of the operation.
 *
 * See mm_modem_command_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
 */
void
mm_modem_command (MMModem *self,
                  const gchar *cmd,
                  guint timeout,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{

    g_return_if_fail (MM_IS_MODEM (self));

    mm_gdbus_modem_call_command (MM_GDBUS_MODEM (self), cmd, timeout, cancellable, callback, user_data);
}

/**
 * mm_modem_command_sync:
 * @self: A #MMModem.
 * @cmd: AT command to run.
 * @timeout: Maximum time to wait for the response, in seconds.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously runs an AT command in the modem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_command() for the asynchronous version of this method.
 *
 * Returns: (transfer full): A newly allocated string with the reply to the
 * command, or #NULL if @error is set. The returned value should be freed
 * with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_command_sync (MMModem *self,
                       const gchar *cmd,
                       guint timeout,
                       GCancellable *cancellable,
                       GError **error)
{
    gchar *result;

    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    if (!mm_gdbus_modem_call_command_sync (MM_GDBUS_MODEM (self), cmd, timeout, &result, cancellable, error))
        return NULL;

    return result;
}

/*****************************************************************************/

/**
 * mm_modem_set_power_state_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_set_power_state().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_set_power_state().
 *
 * Returns: %TRUE if the power state was successfully set, %FALSE if @error is
 * set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_set_power_state_finish (MMModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_power_state_finish (MM_GDBUS_MODEM (self), res, error);
}

/**
 * mm_modem_set_power_state:
 * @self: A #MMModem.
 * @state: Either %MM_MODEM_POWER_STATE_LOW or %MM_MODEM_POWER_STATE_ON. Every
 *  other #MMModemPowerState value is not allowed.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously sets the power state of the device. This method can only be
 * used while the modem is in %MM_MODEM_STATE_DISABLED state.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_set_power_state_finish() to get the result of the operation.
 *
 * See mm_modem_set_power_state_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
 */
void
mm_modem_set_power_state (MMModem *self,
                          MMModemPowerState state,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));

    mm_gdbus_modem_call_set_power_state (MM_GDBUS_MODEM (self), state, cancellable, callback, user_data);
}

/**
 * mm_modem_set_power_state_sync:
 * @self: A #MMModem.
 * @state: Either %MM_MODEM_POWER_STATE_LOW or %MM_MODEM_POWER_STATE_ON. Every
 *  other #MMModemPowerState value is not allowed.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously sets the power state of the device. This method can only be
 * used while the modem is in %MM_MODEM_STATE_DISABLED state.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_set_power_state() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the power state was successfully set, %FALSE if @error is
 * set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_set_power_state_sync (MMModem *self,
                               MMModemPowerState state,
                               GCancellable *cancellable,
                               GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_power_state_sync (MM_GDBUS_MODEM (self), state, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_set_current_capabilities_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_set_current_capabilities().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_set_current_capabilities().
 *
 * Returns: %TRUE if the capabilities were successfully set, %FALSE if @error is
 * set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_set_current_capabilities_finish (MMModem *self,
                                          GAsyncResult *res,
                                          GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_current_capabilities_finish (MM_GDBUS_MODEM (self), res, error);
}

/**
 * mm_modem_set_current_capabilities:
 * @self: A #MMModem.
 * @capabilities: A #MMModemCapability mask.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously sets the capabilities of the device. A restart of the modem
 * may be required.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_set_current_capabilities_finish() to get the result of the
 * operation.
 *
 * See mm_modem_set_current_capabilities_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.0
 */
void
mm_modem_set_current_capabilities (MMModem *self,
                                   MMModemCapability capabilities,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));

    mm_gdbus_modem_call_set_current_capabilities (MM_GDBUS_MODEM (self),
                                                  capabilities,
                                                  cancellable,
                                                  callback,
                                                  user_data);
}

/**
 * mm_modem_set_current_capabilities_sync:
 * @self: A #MMModem.
 * @capabilities: A #MMModemCapability mask.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously sets the capabilities of the device. A restart of the modem may
 * be required.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_set_current_capabilities() for the asynchronous version of this
 * method.
 *
 * Returns: %TRUE if the capabilities were successfully set, %FALSE if @error is
 * set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_set_current_capabilities_sync (MMModem *self,
                                        MMModemCapability capabilities,
                                        GCancellable *cancellable,
                                        GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return (mm_gdbus_modem_call_set_current_capabilities_sync (
                MM_GDBUS_MODEM (self),
                capabilities,
                cancellable,
                error));
}

/*****************************************************************************/

/**
 * mm_modem_set_current_modes_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_set_current_modes().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_set_current_modes().
 *
 * Returns: %TRUE if the allowed modes were successfully set, %FALSE if @error
 * is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_set_current_modes_finish (MMModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_current_modes_finish (MM_GDBUS_MODEM (self), res, error);
}

/**
 * mm_modem_set_current_modes:
 * @self: A #MMModem.
 * @modes: Mask of #MMModemMode values specifying which modes are allowed.
 * @preferred: A #MMModemMode value specifying which of the modes given in
 *  @modes is the preferred one, or #MM_MODEM_MODE_NONE if none.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously sets the access technologies (e.g. 2G/3G/4G preference) the
 * device is currently allowed to use when connecting to a network.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_set_current_modes_finish() to get the result of the operation.
 *
 * See mm_modem_set_current_modes_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.0
 */
void
mm_modem_set_current_modes (MMModem *self,
                            MMModemMode modes,
                            MMModemMode preferred,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));

    mm_gdbus_modem_call_set_current_modes (MM_GDBUS_MODEM (self),
                                           g_variant_new ("(uu)", modes, preferred),
                                           cancellable,
                                           callback,
                                           user_data);
}

/**
 * mm_modem_set_current_modes_sync:
 * @self: A #MMModem.
 * @modes: Mask of #MMModemMode values specifying which modes are allowed.
 * @preferred: A #MMModemMode value specifying which of the modes given in
 *  @modes is the preferred one, or #MM_MODEM_MODE_NONE if none.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously sets the access technologies (e.g. 2G/3G/4G preference) the
 * device is currently allowed to use when connecting to a network.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_set_current_modes() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the allowed modes were successfully set, %FALSE if @error
 * is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_set_current_modes_sync (MMModem *self,
                                 MMModemMode modes,
                                 MMModemMode preferred,
                                 GCancellable *cancellable,
                                 GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_current_modes_sync (MM_GDBUS_MODEM (self),
                                                       g_variant_new ("(uu)", modes, preferred),
                                                       cancellable,
                                                       error);
}

/*****************************************************************************/

/**
 * mm_modem_set_current_bands_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_set_current_bands().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_set_current_bands().
 *
 * Returns: %TRUE if the bands were successfully set, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_set_current_bands_finish (MMModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_current_bands_finish (MM_GDBUS_MODEM (self), res, error);
}

/**
 * mm_modem_set_current_bands:
 * @self: A #MMModem.
 * @bands: An array of #MMModemBand values specifying which bands are allowed.
 * @n_bands: Number of elements in @bands.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously sets the radio frequency and technology bands the device is
 * currently allowed to use when connecting to a network.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_set_current_bands_finish() to get the result of the operation.
 *
 * See mm_modem_set_current_bands_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.0
 */
void
mm_modem_set_current_bands (MMModem *self,
                            const MMModemBand *bands,
                            guint n_bands,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));

    mm_gdbus_modem_call_set_current_bands (MM_GDBUS_MODEM (self),
                                           mm_common_bands_array_to_variant (bands, n_bands),
                                           cancellable,
                                           callback,
                                           user_data);
}

/**
 * mm_modem_set_current_bands_sync:
 * @self: A #MMModem.
 * @bands: An array of #MMModemBand values specifying which bands are allowed.
 * @n_bands: Number of elements in @bands.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously sets the radio frequency and technology bands the device is
 * currently allowed to use when connecting to a network.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_set_current_bands() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the bands were successfully set, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_set_current_bands_sync (MMModem *self,
                                 const MMModemBand *bands,
                                 guint n_bands,
                                 GCancellable *cancellable,
                                 GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return (mm_gdbus_modem_call_set_current_bands_sync (
                MM_GDBUS_MODEM (self),
                mm_common_bands_array_to_variant (bands, n_bands),
                cancellable,
                error));
}

/*****************************************************************************/

/**
 * mm_modem_get_sim_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_get_sim().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_get_sim().
 *
 * Returns: (transfer full): a #MMSim or #NULL if @error is set. The returned
 * value should be freed with g_object_unref().
 *
 * Since: 1.0
 */
MMSim *
mm_modem_get_sim_finish (MMModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_get_sim_ready (GDBusConnection *connection,
                     GAsyncResult *res,
                     GTask *task)
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
 * mm_modem_get_sim:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the #MMSim object managed by this #MMModem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_get_sim_finish() to get the result of the operation.
 *
 * See mm_modem_get_sim_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
 */
void
mm_modem_get_sim (MMModem *self,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GTask *task;
    const gchar *sim_path;

    g_return_if_fail (MM_IS_MODEM (self));

    task = g_task_new (self, cancellable, callback, user_data);

    sim_path = mm_modem_get_sim_path (self);
    if (!sim_path || g_str_equal (sim_path, "/")) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_NOT_FOUND,
                                 "No SIM object available");
        g_object_unref (task);
        return;
    }

    g_async_initable_new_async (MM_TYPE_SIM,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                (GAsyncReadyCallback)modem_get_sim_ready,
                                task,
                                "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                "g-name",           MM_DBUS_SERVICE,
                                "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                "g-object-path",    sim_path,
                                "g-interface-name", "org.freedesktop.ModemManager1.Sim",
                                NULL);
}

/**
 * mm_modem_get_sim_sync:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously gets the #MMSim object managed by this #MMModem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_get_sim() for the asynchronous version of this method.
 *
 * Returns: (transfer full): a #MMSim or #NULL if @error is set. The returned
 * value should be freed with g_object_unref().
 *
 * Since: 1.0
 */
MMSim *
mm_modem_get_sim_sync (MMModem *self,
                       GCancellable *cancellable,
                       GError **error)
{
    GObject *sim;
    const gchar *sim_path;

    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    sim_path = mm_modem_get_sim_path (self);
    if (!sim_path || g_str_equal (sim_path, "/")) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "No SIM object available");
        return NULL;
    }

    sim = g_initable_new (MM_TYPE_SIM,
                          cancellable,
                          error,
                          "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                          "g-name",           MM_DBUS_SERVICE,
                          "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                          "g-object-path",    sim_path,
                          "g-interface-name", "org.freedesktop.ModemManager1.Sim",
                          NULL);

    return (sim ? MM_SIM (sim) : NULL);
}

/*****************************************************************************/

typedef struct {
    gchar     **sim_paths;
    GPtrArray  *sim_slots;
    guint       n_sim_paths;
    guint       i;
} ListSimSlotsContext;

static void
list_sim_slots_context_free (ListSimSlotsContext *ctx)
{
    g_strfreev (ctx->sim_paths);
    g_ptr_array_unref (ctx->sim_slots);
    g_slice_free (ListSimSlotsContext, ctx);
}

/**
 * mm_modem_list_sim_slots_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_list_sim_slots().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_list_sim_slots().
 *
 * Returns: (transfer full) (element-type ModemManager.Sim): The array of
 * #MMSim objects, or %NULL if @error is set.
 *
 * Since: 1.16
 */
GPtrArray *
mm_modem_list_sim_slots_finish (MMModem       *self,
                                GAsyncResult  *res,
                                GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
sim_slot_free (MMSim *sim)
{
    if (sim)
        g_object_unref (sim);
}

static void create_next_sim (GTask *task);

static void
modem_list_sim_slots_build_object_ready (GDBusConnection *connection,
                                         GAsyncResult    *res,
                                         GTask           *task)
{
    GObject             *sim;
    GError              *error = NULL;
    GObject             *source_object;
    ListSimSlotsContext *ctx;

    ctx = g_task_get_task_data (task);

    source_object = g_async_result_get_source_object (res);
    sim = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, &error);
    g_object_unref (source_object);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_ptr_array_add (ctx->sim_slots, sim);

    /* Keep on creating next object */
    ctx->i++;
    create_next_sim (task);
}

static void
create_next_sim (GTask *task)
{
    MMModem             *self;
    ListSimSlotsContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* If no more additional sims, just end here. */
    if (ctx->i == ctx->n_sim_paths) {
        g_assert_cmpuint (ctx->n_sim_paths, ==, ctx->sim_slots->len);
        g_task_return_pointer (task, g_steal_pointer (&ctx->sim_slots), (GDestroyNotify)g_ptr_array_unref);
        g_object_unref (task);
        return;
    }

    /* Empty slot? */
    if (g_str_equal (ctx->sim_paths[ctx->i], "/")) {
        g_ptr_array_add (ctx->sim_slots, NULL);
        ctx->i++;
        create_next_sim (task);
        return;
    }

    g_async_initable_new_async (MM_TYPE_SIM,
                                G_PRIORITY_DEFAULT,
                                g_task_get_cancellable (task),
                                (GAsyncReadyCallback)modem_list_sim_slots_build_object_ready,
                                task,
                                "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                "g-name",           MM_DBUS_SERVICE,
                                "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                "g-object-path",    ctx->sim_paths[ctx->i],
                                "g-interface-name", "org.freedesktop.ModemManager1.Sim",
                                NULL);
}

/**
 * mm_modem_list_sim_slots:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously lists the SIM slots available in the #MMModem.
 *
 * The returned array contains one element per slot available in the system;
 * a #MMSim in each of the slots that contains a valid SIM card or %NULL if
 * no SIM card is found.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_list_sim_slots_finish() to get the result of the operation.
 *
 * See mm_modem_list_sim_slots_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.16
 */
void
mm_modem_list_sim_slots (MMModem             *self,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    ListSimSlotsContext *ctx;
    GTask               *task;

    g_return_if_fail (MM_IS_MODEM (self));

    ctx = g_slice_new0 (ListSimSlotsContext);
    ctx->sim_paths = mm_gdbus_modem_dup_sim_slots (MM_GDBUS_MODEM (self));

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)list_sim_slots_context_free);

    /* If no sim slots, just end here. */
    if (!ctx->sim_paths) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_NOT_FOUND,
                                 "No SIM slots available");
        g_object_unref (task);
        return;
    }

    /* Got list of paths, start creating objects for each */
    ctx->n_sim_paths = g_strv_length (ctx->sim_paths);
    ctx->sim_slots = g_ptr_array_new_full (ctx->n_sim_paths, (GDestroyNotify)sim_slot_free);
    ctx->i = 0;
    create_next_sim (task);
}

/**
 * mm_modem_list_sim_slots_sync:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously lists the SIM slots available in the #MMModem.
 *
 * The returned array contains one element per slot available in the system;
 * a #MMSim in each of the slots that contains a valid SIM card or %NULL if
 * no SIM card is found.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_list_sim_slots() for the asynchronous version of this method.
 *
 * Returns: (transfer full) (element-type ModemManager.Sim): The array of
 * #MMSim objects, or %NULL if @error is set.
 *
 * Since: 1.16
 */
GPtrArray *
mm_modem_list_sim_slots_sync (MMModem       *self,
                              GCancellable  *cancellable,
                              GError       **error)
{
    g_autoptr(GPtrArray) sim_slots = NULL;
    g_auto(GStrv)        sim_paths = NULL;
    guint                n_sim_paths;
    guint                i;

    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    sim_paths = mm_gdbus_modem_dup_sim_slots (MM_GDBUS_MODEM (self));

    /* Only non-empty lists are returned */
    if (!sim_paths)
        return NULL;

    n_sim_paths = g_strv_length (sim_paths);

    sim_slots = g_ptr_array_new_full (n_sim_paths, (GDestroyNotify)sim_slot_free);
    for (i = 0; i < n_sim_paths; i++) {
        GObject *sim;

        if (g_str_equal (sim_paths[i], "/")) {
            g_ptr_array_add (sim_slots, NULL);
            continue;
        }

        sim = g_initable_new (MM_TYPE_SIM,
                              cancellable,
                              error,
                              "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                              "g-name",           MM_DBUS_SERVICE,
                              "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                              "g-object-path",    sim_paths[i],
                              "g-interface-name", "org.freedesktop.ModemManager1.Sim",
                              NULL);
        if (!sim)
            return NULL;

        /* Keep the object */
        g_ptr_array_add (sim_slots, sim);
    }
    g_assert_cmpuint (sim_slots->len, ==, n_sim_paths);

    return g_steal_pointer (&sim_slots);
}

/*****************************************************************************/

/**
 * mm_modem_set_primary_sim_slot_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_set_primary_sim_slot().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_set_primary_sim_slot().
 *
 * Returns: %TRUE if the SIM slot switch has been successfully requested, %FALSE if
 * @error is set.
 *
 * Since: 1.16
 */
gboolean
mm_modem_set_primary_sim_slot_finish (MMModem       *self,
                                      GAsyncResult  *res,
                                      GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_primary_sim_slot_finish (MM_GDBUS_MODEM (self), res, error);
}

/**
 * mm_modem_set_primary_sim_slot:
 * @self: A #MMModem.
 * @sim_slot: SIM slot number.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to select which SIM slot to be considered as primary.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_set_primary_sim_slot_finish() to get the result of the operation.
 *
 * See mm_modem_set_primary_sim_slot_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.16
 */
void
mm_modem_set_primary_sim_slot (MMModem             *self,
                               guint                sim_slot,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));

    mm_gdbus_modem_call_set_primary_sim_slot (MM_GDBUS_MODEM (self), sim_slot, cancellable, callback, user_data);
}

/**
 * mm_modem_set_primary_sim_slot_sync:
 * @self: A #MMModem.
 * @sim_slot: SIM slot number.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to select which SIM slot to be considered as primary.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_set_primary_sim_slot() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the SIM slot switch has been successfully requested, %FALSE if
 * @error is set.
 *
 * Since: 1.16
 */
gboolean
mm_modem_set_primary_sim_slot_sync (MMModem       *self,
                                    guint          sim_slot,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_primary_sim_slot_sync (MM_GDBUS_MODEM (self), sim_slot, cancellable, error);
}

/*****************************************************************************/

static void
mm_modem_init (MMModem *self)
{
    /* Setup private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_MODEM,
                                              MMModemPrivate);
    g_mutex_init (&self->priv->unlock_retries_mutex);
    g_mutex_init (&self->priv->supported_modes_mutex);
    g_mutex_init (&self->priv->supported_capabilities_mutex);
    g_mutex_init (&self->priv->supported_bands_mutex);
    g_mutex_init (&self->priv->current_bands_mutex);
    g_mutex_init (&self->priv->ports_mutex);
}

static void
finalize (GObject *object)
{
    MMModem *self = MM_MODEM (object);

    g_mutex_clear (&self->priv->unlock_retries_mutex);
    g_mutex_clear (&self->priv->supported_modes_mutex);
    g_mutex_clear (&self->priv->supported_capabilities_mutex);
    g_mutex_clear (&self->priv->supported_bands_mutex);
    g_mutex_clear (&self->priv->current_bands_mutex);
    g_mutex_clear (&self->priv->ports_mutex);

    if (self->priv->supported_modes)
        g_array_unref (self->priv->supported_modes);
    if (self->priv->supported_capabilities)
        g_array_unref (self->priv->supported_capabilities);
    if (self->priv->supported_bands)
        g_array_unref (self->priv->supported_bands);
    if (self->priv->current_bands)
        g_array_unref (self->priv->current_bands);
    if (self->priv->ports)
       g_array_unref (self->priv->ports);

    G_OBJECT_CLASS (mm_modem_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMModem *self = MM_MODEM (object);

    g_clear_object (&self->priv->unlock_retries);

    G_OBJECT_CLASS (mm_modem_parent_class)->dispose (object);
}

static void
mm_modem_class_init (MMModemClass *modem_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (modem_class);

    g_type_class_add_private (object_class, sizeof (MMModemPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
    object_class->finalize = finalize;
}
