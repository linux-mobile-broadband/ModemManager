/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-firmware-update-settings.h"

/**
 * SECTION: mm-firmware-update-settings
 * @title: MMFirmwareUpdateSettings
 * @short_description: Helper object to handle firmware update settings.
 *
 * The #MMFirmwareUpdateSettings is an object handling the settings exposed to
 * aid in the firmware update operation.
 */

G_DEFINE_TYPE (MMFirmwareUpdateSettings, mm_firmware_update_settings, G_TYPE_OBJECT)

#define PROPERTY_DEVICE_IDS  "device-ids"
#define PROPERTY_VERSION     "version"
#define PROPERTY_FASTBOOT_AT "fastboot-at"

struct _MMFirmwareUpdateSettingsPrivate {
    /* Generic */
    MMModemFirmwareUpdateMethod   method;
    gchar                       **device_ids;
    gchar                        *version;
    /* Fasboot specific */
    gchar *fastboot_at;
};

/*****************************************************************************/

/**
 * mm_firmware_update_settings_get_method:
 * @self: A #MMFirmwareUpdateSettings.
 *
 * Gets the methods to use during the firmware update operation.
 *
 * Returns: a bitmask of #MMModemFirmwareUpdateMethod values.
 *
 * Since: 1.10
 */
MMModemFirmwareUpdateMethod
mm_firmware_update_settings_get_method (MMFirmwareUpdateSettings *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_UPDATE_SETTINGS (self), MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE);

    return self->priv->method;
}

/*****************************************************************************/

/**
 * mm_firmware_update_settings_get_device_ids:
 * @self: a #MMFirmwareUpdateSettings.
 *
 * Gets the list of device ids used to identify the device during a firmware
 * update operation.
 *
 * Returns: (transfer none): The list of device ids, or %NULL if unknown. Do not
 * free the returned value, it is owned by @self.
 *
 * Since: 1.10
 */
const gchar **
mm_firmware_update_settings_get_device_ids (MMFirmwareUpdateSettings *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_UPDATE_SETTINGS (self), NULL);

    return (const gchar **) self->priv->device_ids;
}

/**
 * mm_firmware_update_settings_set_device_ids: (skip)
 */
void
mm_firmware_update_settings_set_device_ids (MMFirmwareUpdateSettings  *self,
                                            const gchar              **device_ids)
{
    g_return_if_fail (MM_IS_FIRMWARE_UPDATE_SETTINGS (self));

    g_strfreev (self->priv->device_ids);
    self->priv->device_ids = g_strdupv ((gchar **)device_ids);
}

/*****************************************************************************/

/**
 * mm_firmware_update_settings_get_version:
 * @self: a #MMFirmwareUpdateSettings.
 *
 * Gets firmware version string.
 *
 *
 * Returns: The version string, or %NULL if unknown. Do not free the returned
 * value, it is owned by @self.
 *
 * Since: 1.10
 */
const gchar *
mm_firmware_update_settings_get_version (MMFirmwareUpdateSettings *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_UPDATE_SETTINGS (self), NULL);

    return self->priv->version;
}

/**
 * mm_firmware_update_settings_set_version: (skip)
 */
void
mm_firmware_update_settings_set_version (MMFirmwareUpdateSettings *self,
                                         const gchar              *version)
{
    g_return_if_fail (MM_IS_FIRMWARE_UPDATE_SETTINGS (self));

    g_free (self->priv->version);
    self->priv->version = g_strdup (version);
}

/*****************************************************************************/

/**
 * mm_firmware_update_settings_get_fastboot_at:
 * @self: a #MMFirmwareUpdateSettings.
 *
 * Gets the AT command that should be sent to the module to trigger a reset
 * into fastboot mode.
 *
 * Only applicable if the update method includes
 * %MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT.
 *
 * Returns: The AT command string, or %NULL if unknown. Do not free the returned
 * value, it is owned by @self.
 *
 * Since: 1.10
 */
const gchar *
mm_firmware_update_settings_get_fastboot_at (MMFirmwareUpdateSettings *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_UPDATE_SETTINGS (self), NULL);
    g_return_val_if_fail (self->priv->method & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT, NULL);

    return self->priv->fastboot_at;
}

/**
 * mm_firmware_update_settings_set_fastboot_at: (skip)
 */
void
mm_firmware_update_settings_set_fastboot_at (MMFirmwareUpdateSettings *self,
                                             const gchar              *fastboot_at)
{
    g_return_if_fail (MM_IS_FIRMWARE_UPDATE_SETTINGS (self));
    g_return_if_fail (self->priv->method & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT);

    g_free (self->priv->fastboot_at);
    self->priv->fastboot_at = g_strdup (fastboot_at);
}

/*****************************************************************************/

/**
 * mm_firmware_update_settings_get_variant: (skip)
 */
GVariant *
mm_firmware_update_settings_get_variant (MMFirmwareUpdateSettings *self)
{
    MMModemFirmwareUpdateMethod method;
    GVariantBuilder             builder;

    method = (self ? self->priv->method : MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("(ua{sv})"));
    g_variant_builder_add (&builder, "u", method);

    g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
    if (self) {
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_DEVICE_IDS,
                               g_variant_new_strv ((const gchar * const *)self->priv->device_ids, -1));

        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_VERSION,
                               g_variant_new_string (self->priv->version));

        if (method & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) {
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   PROPERTY_FASTBOOT_AT,
                                   g_variant_new_string (self->priv->fastboot_at));
        }
    }
    g_variant_builder_close (&builder);

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

static gboolean
consume_variant (MMFirmwareUpdateSettings  *self,
                 const gchar               *key,
                 GVariant                  *value,
                 GError                   **error)
{
    if (g_str_equal (key, PROPERTY_FASTBOOT_AT)) {
        g_free (self->priv->fastboot_at);
        self->priv->fastboot_at = g_variant_dup_string (value, NULL);
    } else if (g_str_equal (key, PROPERTY_VERSION)) {
        g_free (self->priv->version);
        self->priv->version = g_variant_dup_string (value, NULL);
    } else if (g_str_equal (key, PROPERTY_DEVICE_IDS)) {
        g_strfreev (self->priv->device_ids);
        self->priv->device_ids = g_variant_dup_strv (value, NULL);
    } else {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid settings dictionary, unexpected key '%s'", key);
        return FALSE;
    }
    return TRUE;
}

/**
 * mm_firmware_update_settings_new_from_variant: (skip)
 */
MMFirmwareUpdateSettings *
mm_firmware_update_settings_new_from_variant (GVariant  *variant,
                                              GError   **error)
{
    MMFirmwareUpdateSettings *self;
    guint                     method = MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE;
    GVariant                 *dictionary = NULL;
    GError                   *inner_error = NULL;

    if (!variant) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "No input given");
        return NULL;
    }

    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE ("(ua{sv})"))) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid input type");
        return NULL;
    }

    g_variant_get (variant, "(u@a{sv})", &method, &dictionary);
    self = mm_firmware_update_settings_new (method);

    if ((method != MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE) && dictionary) {
        GVariantIter  iter;
        gchar        *key;
        GVariant     *value;

        g_variant_iter_init (&iter, dictionary);
        while (!inner_error && g_variant_iter_next (&iter, "{sv}", &key, &value)) {
            consume_variant (self, key, value, &inner_error);
            g_free (key);
            g_variant_unref (value);
        }

        if (!inner_error) {
            if (!self->priv->device_ids)
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                           "Missing required '" PROPERTY_DEVICE_IDS "' setting");
            else if (!self->priv->version)
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                           "Missing required '" PROPERTY_VERSION "' setting");
        }

        if (!inner_error) {
            if ((method & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT) && (!self->priv->fastboot_at))
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                           "Fastboot method requires the '" PROPERTY_FASTBOOT_AT "' setting");
        }
        g_variant_unref (dictionary);
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (self);
        return NULL;
    }

    return self;
}

/*****************************************************************************/

/**
 * mm_firmware_update_settings_new: (skip)
 */
MMFirmwareUpdateSettings *
mm_firmware_update_settings_new (MMModemFirmwareUpdateMethod method)
{
    MMFirmwareUpdateSettings *self;

    self = g_object_new (MM_TYPE_FIRMWARE_UPDATE_SETTINGS, NULL);
    self->priv->method = method;
    return self;
}

static void
mm_firmware_update_settings_init (MMFirmwareUpdateSettings *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_FIRMWARE_UPDATE_SETTINGS, MMFirmwareUpdateSettingsPrivate);
    self->priv->method = MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE;
}

static void
finalize (GObject *object)
{
    MMFirmwareUpdateSettings *self = MM_FIRMWARE_UPDATE_SETTINGS (object);

    g_strfreev (self->priv->device_ids);
    g_free (self->priv->version);
    g_free (self->priv->fastboot_at);

    G_OBJECT_CLASS (mm_firmware_update_settings_parent_class)->finalize (object);
}

static void
mm_firmware_update_settings_class_init (MMFirmwareUpdateSettingsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMFirmwareUpdateSettingsPrivate));

    object_class->finalize = finalize;
}
