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
 * Copyright (C) 2012 Google Inc.
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-firmware-properties.h"

/**
 * SECTION: mm-firmware-properties
 * @title: MMFirmwareProperties
 * @short_description: Helper object to handle firmware information.
 *
 * The #MMFirmwareProperties is an object handling the properties exposed for
 * available firmware images.
 *
 * This object is retrieved with either mm_modem_firmware_list()
 * or mm_modem_firmware_list_sync().
 */

G_DEFINE_TYPE (MMFirmwareProperties, mm_firmware_properties, G_TYPE_OBJECT)

#define PROPERTY_UNIQUE_ID            "unique-id"
#define PROPERTY_IMAGE_TYPE           "image-type"
#define PROPERTY_GOBI_PRI_VERSION     "gobi-pri-version"
#define PROPERTY_GOBI_PRI_INFO        "gobi-pri-info"
#define PROPERTY_GOBI_BOOT_VERSION    "gobi-boot-version"
#define PROPERTY_GOBI_PRI_UNIQUE_ID   "gobi-pri-unique-id"
#define PROPERTY_GOBI_MODEM_UNIQUE_ID "gobi-modem-unique-id"

struct _MMFirmwarePropertiesPrivate {
    /* Mandatory parameters */
    MMFirmwareImageType image_type;
    gchar *unique_id;

    /* Gobi specific */
    gchar *gobi_pri_version;
    gchar *gobi_pri_info;
    gchar *gobi_boot_version;
    gchar *gobi_pri_unique_id;
    gchar *gobi_modem_unique_id;
};

static MMFirmwareProperties *firmware_properties_new_empty (void);

/*****************************************************************************/

/**
 * mm_firmware_properties_get_unique_id:
 * @self: A #MMFirmwareProperties.
 *
 * Gets the unique ID of the firmare image.
 *
 * Returns: (transfer none): The ID of the image. Do not free the returned
 * value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_firmware_properties_get_unique_id (MMFirmwareProperties *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), NULL);

    return self->priv->unique_id;
}

/*****************************************************************************/

/**
 * mm_firmware_properties_get_image_type:
 * @self: A #MMFirmwareProperties.
 *
 * Gets the type of the firmare image.
 *
 * Returns: A #MMFirmwareImageType specifying The type of the image.
 *
 * Since: 1.0
 */
MMFirmwareImageType
mm_firmware_properties_get_image_type (MMFirmwareProperties *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), MM_FIRMWARE_IMAGE_TYPE_UNKNOWN);

    return self->priv->image_type;
}

/*****************************************************************************/

/**
 * mm_firmware_properties_get_gobi_pri_version:
 * @self: a #MMFirmwareProperties.
 *
 * Gets the PRI version of a firmware image of type %MM_FIRMWARE_IMAGE_TYPE_GOBI.
 *
 * Returns: The PRI version, or %NULL if unknown. Do not free the returned value,
 * it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_firmware_properties_get_gobi_pri_version (MMFirmwareProperties *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), NULL);
    g_return_val_if_fail (self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_GOBI, NULL);

    return self->priv->gobi_pri_version;
}

/*
 * mm_firmware_properties_set_gobi_pri_version: (skip)
 */
void
mm_firmware_properties_set_gobi_pri_version (MMFirmwareProperties *self,
                                             const gchar *version)
{
    g_return_if_fail (MM_IS_FIRMWARE_PROPERTIES (self));
    g_return_if_fail (self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_GOBI);

    g_free (self->priv->gobi_pri_version);
    self->priv->gobi_pri_version = g_strdup (version);
}

/*****************************************************************************/

/**
 * mm_firmware_properties_get_gobi_pri_info:
 * @self: a #MMFirmwareProperties.
 *
 * Gets the PRI info of a firmware image of type %MM_FIRMWARE_IMAGE_TYPE_GOBI.
 *
 * Returns: The PRI info, or %NULL if unknown. Do not free the returned value,
 * it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_firmware_properties_get_gobi_pri_info (MMFirmwareProperties *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), NULL);
    g_return_val_if_fail (self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_GOBI, NULL);

    return self->priv->gobi_pri_info;
}

/*
 * mm_firmware_properties_set_gobi_pri_info: (skip)
 */
void
mm_firmware_properties_set_gobi_pri_info (MMFirmwareProperties *self,
                                          const gchar *info)
{
    g_return_if_fail (MM_IS_FIRMWARE_PROPERTIES (self));
    g_return_if_fail (self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_GOBI);

    g_free (self->priv->gobi_pri_info);
    self->priv->gobi_pri_info = g_strdup (info);
}

/**
 * mm_firmware_properties_get_gobi_boot_version:
 * @self: a #MMFirmwareProperties.
 *
 * Gets the boot version of a firmware image of type
 * %MM_FIRMWARE_IMAGE_TYPE_GOBI.
 *
 * Returns: The boot version, or %NULL if unknown. Do not free the returned
 * value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_firmware_properties_get_gobi_boot_version (MMFirmwareProperties *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), NULL);
    g_return_val_if_fail (self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_GOBI, NULL);

    return self->priv->gobi_boot_version;
}

/*
 * mm_firmware_properties_set_gobi_boot_version: (skip)
 */
void
mm_firmware_properties_set_gobi_boot_version (MMFirmwareProperties *self,
                                              const gchar *version)
{
    g_return_if_fail (MM_IS_FIRMWARE_PROPERTIES (self));
    g_return_if_fail (self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_GOBI);

    g_free (self->priv->gobi_boot_version);
    self->priv->gobi_boot_version = g_strdup (version);
}

/*****************************************************************************/

/**
 * mm_firmware_properties_get_gobi_pri_unique_id:
 * @self: a #MMFirmwareProperties.
 *
 * Gets the PRI unique ID of a firmware image of type
 * %MM_FIRMWARE_IMAGE_TYPE_GOBI.
 *
 * Returns: The PRI unique ID, or %NULL if unknown. Do not free the returned
 * value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_firmware_properties_get_gobi_pri_unique_id (MMFirmwareProperties *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), NULL);
    g_return_val_if_fail (self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_GOBI, NULL);

    return self->priv->gobi_pri_unique_id;
}

/*
 * mm_firmware_properties_set_gobi_pri_unique_id: (skip)
 */
void
mm_firmware_properties_set_gobi_pri_unique_id (MMFirmwareProperties *self,
                                               const gchar *unique_id)
{
    g_return_if_fail (MM_IS_FIRMWARE_PROPERTIES (self));
    g_return_if_fail (self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_GOBI);

    g_free (self->priv->gobi_pri_unique_id);
    self->priv->gobi_pri_unique_id = g_strdup (unique_id);
}

/*****************************************************************************/

/**
 * mm_firmware_properties_get_gobi_modem_unique_id:
 * @self: a #MMFirmwareProperties.
 *
 * Gets the MODEM unique ID of a firmware image of type
 * %MM_FIRMWARE_IMAGE_TYPE_GOBI.
 *
 * Returns: The PRI unique ID, or %NULL if unknown. Do not free the returned
 * value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_firmware_properties_get_gobi_modem_unique_id (MMFirmwareProperties *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), NULL);
    g_return_val_if_fail (self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_GOBI, NULL);

    return self->priv->gobi_modem_unique_id;
}

/*
 * mm_firmware_properties_set_gobi_modem_unique_id: (skip)
 */
void
mm_firmware_properties_set_gobi_modem_unique_id (MMFirmwareProperties *self,
                                                 const gchar *unique_id)
{
    g_return_if_fail (MM_IS_FIRMWARE_PROPERTIES (self));
    g_return_if_fail (self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_GOBI);

    g_free (self->priv->gobi_modem_unique_id);
    self->priv->gobi_modem_unique_id = g_strdup (unique_id);
}

/*****************************************************************************/

/*
 * mm_firmware_properties_get_dictionary: (skip)
 */
GVariant *
mm_firmware_properties_get_dictionary (MMFirmwareProperties *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_UNIQUE_ID,
                           g_variant_new_string (self->priv->unique_id));

    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_IMAGE_TYPE,
                           g_variant_new_uint32 (self->priv->image_type));

    if (self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_GOBI) {
        if (self->priv->gobi_pri_version)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   PROPERTY_GOBI_PRI_VERSION,
                                   g_variant_new_string (self->priv->gobi_pri_version));
        if (self->priv->gobi_pri_info)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   PROPERTY_GOBI_PRI_INFO,
                                   g_variant_new_string (self->priv->gobi_pri_info));
        if (self->priv->gobi_boot_version)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   PROPERTY_GOBI_BOOT_VERSION,
                                   g_variant_new_string (self->priv->gobi_boot_version));
        if (self->priv->gobi_pri_unique_id)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   PROPERTY_GOBI_PRI_UNIQUE_ID,
                                   g_variant_new_string (self->priv->gobi_pri_unique_id));
        if (self->priv->gobi_modem_unique_id)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   PROPERTY_GOBI_MODEM_UNIQUE_ID,
                                   g_variant_new_string (self->priv->gobi_modem_unique_id));
    }

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

static gboolean
consume_variant (MMFirmwareProperties *self,
                 const gchar *key,
                 GVariant *value,
                 GError **error)
{
    if (g_str_equal (key, PROPERTY_UNIQUE_ID)) {
        g_free (self->priv->unique_id);
        self->priv->unique_id = g_variant_dup_string (value, NULL);
    } else if (g_str_equal (key, PROPERTY_IMAGE_TYPE)) {
        self->priv->image_type = g_variant_get_uint32 (value);
    } else if (g_str_equal (key, PROPERTY_GOBI_PRI_VERSION)) {
        g_free (self->priv->gobi_pri_version);
        self->priv->gobi_pri_version = g_variant_dup_string (value, NULL);
    } else if (g_str_equal (key, PROPERTY_GOBI_PRI_INFO)) {
        g_free (self->priv->gobi_pri_info);
        self->priv->gobi_pri_info = g_variant_dup_string (value, NULL);
    } else if (g_str_equal (key, PROPERTY_GOBI_BOOT_VERSION)) {
        g_free (self->priv->gobi_boot_version);
        self->priv->gobi_boot_version = g_variant_dup_string (value, NULL);
    } else if (g_str_equal (key, PROPERTY_GOBI_PRI_UNIQUE_ID)) {
        g_free (self->priv->gobi_pri_unique_id);
        self->priv->gobi_pri_unique_id = g_variant_dup_string (value, NULL);
    } else if (g_str_equal (key, PROPERTY_GOBI_MODEM_UNIQUE_ID)) {
        g_free (self->priv->gobi_modem_unique_id);
        self->priv->gobi_modem_unique_id = g_variant_dup_string (value, NULL);
    }
    else {
        /* Set error */
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid properties dictionary, unexpected key '%s'",
                     key);
        return FALSE;
    }

    return TRUE;
}

/*
 * mm_firmware_properties_new_from_dictionary: (skip)
 */
MMFirmwareProperties *
mm_firmware_properties_new_from_dictionary (GVariant *dictionary,
                                            GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMFirmwareProperties *self;

    if (!dictionary) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create Firmware properties from empty dictionary");
        return NULL;
    }

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create Firmware properties from dictionary: "
                     "invalid variant type received");
        return NULL;
    }

    self = firmware_properties_new_empty ();

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        consume_variant (self,
                         key,
                         value,
                         &inner_error);
        g_free (key);
        g_variant_unref (value);
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (self);
        return NULL;
    }

    /* If mandatory properties missing, destroy the object */
    if (!self->priv->unique_id ||
        self->priv->image_type == MM_FIRMWARE_IMAGE_TYPE_UNKNOWN) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create Firmware properties from dictionary: "
                     "mandatory parameter missing");
        g_object_unref (self);
        return NULL;
    }

    return self;
}

/*****************************************************************************/

/*
 * mm_firmware_properties_new: (skip)
 */
MMFirmwareProperties *
mm_firmware_properties_new (MMFirmwareImageType image_type,
                            const gchar *unique_id)
{
    MMFirmwareProperties *self;

    g_return_val_if_fail (image_type != MM_FIRMWARE_IMAGE_TYPE_UNKNOWN, NULL);
    g_return_val_if_fail (unique_id != NULL, NULL);

    self = firmware_properties_new_empty ();
    self->priv->image_type = image_type;
    self->priv->unique_id = g_strdup (unique_id);

    return self;
}

static MMFirmwareProperties *
firmware_properties_new_empty (void)
{
    return (MM_FIRMWARE_PROPERTIES (
                g_object_new (MM_TYPE_FIRMWARE_PROPERTIES, NULL)));
}

static void
mm_firmware_properties_init (MMFirmwareProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_FIRMWARE_PROPERTIES,
                                              MMFirmwarePropertiesPrivate);

    /* Some defaults */
    self->priv->image_type = MM_FIRMWARE_IMAGE_TYPE_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMFirmwareProperties *self = MM_FIRMWARE_PROPERTIES (object);

    g_free (self->priv->unique_id);
    g_free (self->priv->gobi_pri_version);
    g_free (self->priv->gobi_pri_info);
    g_free (self->priv->gobi_boot_version);
    g_free (self->priv->gobi_pri_unique_id);
    g_free (self->priv->gobi_modem_unique_id);

    G_OBJECT_CLASS (mm_firmware_properties_parent_class)->finalize (object);
}

static void
mm_firmware_properties_class_init (MMFirmwarePropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMFirmwarePropertiesPrivate));

    object_class->finalize = finalize;
}
