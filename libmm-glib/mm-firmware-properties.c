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
 * Copyright (C) 2012 Google Inc.
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-firmware-properties.h"

G_DEFINE_TYPE (MMFirmwareProperties, mm_firmware_properties, G_TYPE_OBJECT);

#define PROPERTY_NAME       "name"
#define PROPERTY_VERSION    "version"
#define PROPERTY_IMAGE_TYPE "image-type"

struct _MMFirmwarePropertiesPrivate {
    /* Mandatory parameters */
    MMFirmwareImageType image_type;
    gchar *name;
    gchar *version;
};

static MMFirmwareProperties *firmware_properties_new_empty (void);

/*****************************************************************************/

/**
 * mm_firmware_properties_get_name:
 * @self: A #MMFirmwareProperties.
 *
 * Gets the unique name of the firmare image.
 *
 * Returns: (transfer none): The name of the image. Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_firmware_properties_get_name (MMFirmwareProperties *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), NULL);

    return self->priv->name;
}

/**
 * mm_firmware_properties_get_version:
 * @self: A #MMFirmwareProperties.
 *
 * Gets the version string of the firmare image.
 *
 * Returns: (transfer none): The version of the image. Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_firmware_properties_get_version (MMFirmwareProperties *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), NULL);

    return self->priv->version;
}

/**
 * mm_firmware_properties_get_image_type:
 * @self: A #MMFirmwareProperties.
 *
 * Gets the type of the firmare image.
 *
 * Returns: A #MMFirmwareImageType specifying The type of the image.
 */
MMFirmwareImageType
mm_firmware_properties_get_image_type (MMFirmwareProperties *self)
{
    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), MM_FIRMWARE_IMAGE_TYPE_UNKNOWN);

    return self->priv->image_type;
}

/*****************************************************************************/

/**
 * mm_firmware_properties_get_dictionary:
 * @self: A #MMFirmwareProperties.
 *
 * Gets a variant dictionary with the contents of @self.
 *
 * Returns: (transfer full): A dictionary with the image properties. The returned value should be freed with g_variant_unref().
 */
GVariant *
mm_firmware_properties_get_dictionary (MMFirmwareProperties *self)
{
    GVariantBuilder builder;

    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), NULL);

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_FIRMWARE_PROPERTIES (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_NAME,
                           g_variant_new_string (self->priv->name));

    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_VERSION,
                           g_variant_new_string (self->priv->version));

    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_IMAGE_TYPE,
                           g_variant_new_uint32 (self->priv->image_type));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

static gboolean
consume_variant (MMFirmwareProperties *self,
                 const gchar *key,
                 GVariant *value,
                 GError **error)
{
    if (g_str_equal (key, PROPERTY_NAME)) {
        g_free (self->priv->name);
        self->priv->name = g_variant_dup_string (value, NULL);
    } else if (g_str_equal (key, PROPERTY_VERSION)) {
        g_free (self->priv->version);
        self->priv->version = g_variant_dup_string (value, NULL);
    } else if (g_str_equal (key, PROPERTY_IMAGE_TYPE))
        self->priv->image_type = g_variant_get_uint32 (value);
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

/**
 * mm_firmware_properties_new_from_dictionary:
 * @dictionary: A variant dictionary with the properties of the image.
 * @error: Return location for error or %NULL.
 *
 * Creates a new #MMFirmwareProperties object with the properties exposed in
 * the dictionary.
 *
 * Returns: (transfer full): A #MMFirmwareProperties or %NULL if @error is set. The returned value should be freed with g_object_unref().
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
    if (!self->priv->name ||
        !self->priv->version ||
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

/**
 * mm_firmware_properties_new:
 * @image_type: A #MMFirmwareImageType specifying the type of the image.
 * @name: The unique name of the image.
 * @version: The version of the image.
 *
 * Creates a new #MMFirmwareProperties object with the properties specified.
 *
 * Returns: (transfer full): A #MMFirmwareProperties or %NULL if @error is set. The returned value should be freed with g_object_unref().
 */
MMFirmwareProperties *
mm_firmware_properties_new (MMFirmwareImageType image_type,
                            const gchar *name,
                            const gchar *version)
{
    MMFirmwareProperties *self;

    g_return_val_if_fail (image_type != MM_FIRMWARE_IMAGE_TYPE_UNKNOWN, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (version != NULL, NULL);

    self = firmware_properties_new_empty ();
    self->priv->image_type = image_type;
    self->priv->name = g_strdup (name);
    self->priv->version = g_strdup (version);

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

    g_free (self->priv->name);
    g_free (self->priv->version);

    G_OBJECT_CLASS (mm_firmware_properties_parent_class)->finalize (object);
}

static void
mm_firmware_properties_class_init (MMFirmwarePropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMFirmwarePropertiesPrivate));

    object_class->finalize = finalize;
}
