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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include <string.h>

#include <libmm-common.h>

#include "mm-common-connect-properties.h"

G_DEFINE_TYPE (MMCommonConnectProperties, mm_common_connect_properties, G_TYPE_OBJECT);

#define PROPERTY_PIN             "pin"
#define PROPERTY_OPERATOR_ID     "operator-id"
#define PROPERTY_ALLOWED_BANDS   "allowed-bands"
#define PROPERTY_ALLOWED_MODES   "allowed-modes"
#define PROPERTY_PREFERRED_MODE  "preferred-mode"
#define PROPERTY_APN             "apn"
#define PROPERTY_USER            "user"
#define PROPERTY_PASSWORD        "password"
#define PROPERTY_IP_TYPE         "ip-type"
#define PROPERTY_NUMBER          "number"
#define PROPERTY_ALLOW_ROAMING   "allow-roaming"

struct _MMCommonConnectPropertiesPrivate {
    /* PIN */
    gchar *pin;
    /* Operator ID */
    gchar *operator_id;
    /* Bands */
    MMModemBand *allowed_bands;
    guint n_allowed_bands;
    /* Modes */
    gboolean allowed_modes_set;
    MMModemMode allowed_modes;
    MMModemMode preferred_mode;
    /* APN */
    gchar *apn;
    /* User */
    gchar *user;
    /* Password */
    gchar *password;
    /* IP type */
    gchar *ip_type;
    /* Number */
    gchar *number;
    /* Roaming allowance */
    gboolean allow_roaming_set;
    gboolean allow_roaming;
};

/*****************************************************************************/

void
mm_common_connect_properties_set_pin (MMCommonConnectProperties *self,
                                      const gchar *pin)
{
    g_free (self->priv->pin);
    self->priv->pin = g_strdup (pin);
}

void
mm_common_connect_properties_set_operator_id (MMCommonConnectProperties *self,
                                              const gchar *operator_id)
{
    g_free (self->priv->operator_id);
    self->priv->operator_id = g_strdup (operator_id);
}

void
mm_common_connect_properties_set_allowed_bands (MMCommonConnectProperties *self,
                                                const MMModemBand *bands,
                                                guint n_bands)
{
    g_free (self->priv->allowed_bands);
    self->priv->n_allowed_bands = n_bands;
    self->priv->allowed_bands = g_new (MMModemBand, self->priv->n_allowed_bands);
    memcpy (self->priv->allowed_bands,
            bands,
            sizeof (MMModemBand) * self->priv->n_allowed_bands);
}

void
mm_common_connect_properties_set_allowed_modes (MMCommonConnectProperties *self,
                                                MMModemMode allowed,
                                                MMModemMode preferred)
{
    self->priv->allowed_modes = allowed;
    self->priv->preferred_mode = preferred;
    self->priv->allowed_modes_set = TRUE;
}

void
mm_common_connect_properties_set_apn (MMCommonConnectProperties *self,
                                      const gchar *apn)
{
    g_free (self->priv->apn);
    self->priv->apn = g_strdup (apn);
}

void
mm_common_connect_properties_set_user (MMCommonConnectProperties *self,
                                       const gchar *user)
{
    g_free (self->priv->user);
    self->priv->user = g_strdup (user);
}

void
mm_common_connect_properties_set_password (MMCommonConnectProperties *self,
                                           const gchar *password)
{
    g_free (self->priv->password);
    self->priv->password = g_strdup (password);
}

void
mm_common_connect_properties_set_ip_type (MMCommonConnectProperties *self,
                                          const gchar *ip_type)
{
    g_free (self->priv->ip_type);
    self->priv->ip_type = g_strdup (ip_type);
}

void
mm_common_connect_properties_set_allow_roaming (MMCommonConnectProperties *self,
                                                gboolean allow_roaming)
{
    self->priv->allow_roaming = allow_roaming;
    self->priv->allow_roaming_set = TRUE;
}

void
mm_common_connect_properties_set_number (MMCommonConnectProperties *self,
                                         const gchar *number)
{
    g_free (self->priv->number);
    self->priv->number = g_strdup (number);
}

/*****************************************************************************/

const gchar *
mm_common_connect_properties_get_pin (MMCommonConnectProperties *self)
{
    return self->priv->apn;
}

const gchar *
mm_common_connect_properties_get_operator_id (MMCommonConnectProperties *self)
{
    return self->priv->operator_id;
}

void
mm_common_connect_properties_get_allowed_bands (MMCommonConnectProperties *self,
                                                const MMModemBand **bands,
                                                guint *n_bands)
{
    *bands = self->priv->allowed_bands;
    *n_bands = self->priv->n_allowed_bands;
}

void
mm_common_connect_properties_get_allowed_modes (MMCommonConnectProperties *self,
                                                MMModemMode *allowed,
                                                MMModemMode *preferred)
{
    *allowed = self->priv->allowed_modes;
    *preferred = self->priv->preferred_mode;
}

const gchar *
mm_common_connect_properties_get_apn (MMCommonConnectProperties *self)
{
    return self->priv->apn;
}

const gchar *
mm_common_connect_properties_get_user (MMCommonConnectProperties *self)
{
    return self->priv->user;
}

const gchar *
mm_common_connect_properties_get_password (MMCommonConnectProperties *self)
{
    return self->priv->password;
}

const gchar *
mm_common_connect_properties_get_ip_type (MMCommonConnectProperties *self)
{
    return self->priv->ip_type;
}

gboolean
mm_common_connect_properties_get_allow_roaming (MMCommonConnectProperties *self)
{
    return self->priv->allow_roaming;
}

const gchar *
mm_common_connect_properties_get_number (MMCommonConnectProperties *self)
{
    return self->priv->number;
}

/*****************************************************************************/

GVariant *
mm_common_connect_properties_get_dictionary (MMCommonConnectProperties *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->pin)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_PIN,
                               g_variant_new_string (self->priv->pin));

    if (self->priv->operator_id)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_OPERATOR_ID,
                               g_variant_new_string (self->priv->operator_id));

    if (self->priv->allowed_bands)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ALLOWED_BANDS,
                               mm_common_bands_array_to_variant (self->priv->allowed_bands,
                                                                 self->priv->n_allowed_bands));

    if (self->priv->allowed_modes_set) {
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ALLOWED_MODES,
                               g_variant_new_uint32 (self->priv->allowed_modes));
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_PREFERRED_MODE,
                               g_variant_new_uint32 (self->priv->preferred_mode));
    }

    if (self->priv->apn)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_APN,
                               g_variant_new_string (self->priv->apn));

    if (self->priv->user)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_USER,
                               g_variant_new_string (self->priv->user));

    if (self->priv->password)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_PASSWORD,
                               g_variant_new_string (self->priv->password));

    if (self->priv->ip_type)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_IP_TYPE,
                               g_variant_new_string (self->priv->ip_type));

    if (self->priv->number)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_NUMBER,
                               g_variant_new_string (self->priv->number));

    if (self->priv->allow_roaming_set)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ALLOW_ROAMING,
                               g_variant_new_boolean (self->priv->allow_roaming));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

MMCommonConnectProperties *
mm_common_connect_properties_new_from_string (const gchar *str,
                                              GError **error)
{
    GError *inner_error = NULL;
    MMCommonConnectProperties *properties;
    gchar **words;
    gchar *key;
    gchar *value;
    guint i;
    const gchar *allowed_modes_str = NULL;
    const gchar *preferred_mode_str = NULL;

    properties = mm_common_connect_properties_new ();

    /* Expecting input as:
     *   key1=string,key2=true,key3=false...
     * */

    words = g_strsplit_set (str, ",= ", -1);
    if (!words)
        return properties;

    i = 0;
    key = words[i];
    while (key) {
        value = words[++i];

        if (!value) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_INVALID_ARGS,
                                       "Invalid properties string, no value for key '%s'",
                                       key);
            break;
        }

        if (g_str_equal (key, PROPERTY_PIN))
            mm_common_connect_properties_set_pin (properties, value);
        else if (g_str_equal (key, PROPERTY_OPERATOR_ID))
            mm_common_connect_properties_set_operator_id (properties, value);
        else if (g_str_equal (key, PROPERTY_APN))
            mm_common_connect_properties_set_apn (properties, value);
        else if (g_str_equal (key, PROPERTY_USER))
            mm_common_connect_properties_set_user (properties, value);
        else if (g_str_equal (key, PROPERTY_PASSWORD))
            mm_common_connect_properties_set_password (properties, value);
        else if (g_str_equal (key, PROPERTY_IP_TYPE))
            mm_common_connect_properties_set_ip_type (properties, value);
        else if (g_str_equal (key, PROPERTY_ALLOW_ROAMING)) {
            gboolean allow_roaming;

            allow_roaming = mm_common_get_boolean_from_string (value, &inner_error);
            if (!inner_error)
                mm_common_connect_properties_set_allow_roaming (properties, allow_roaming);
        } else if (g_str_equal (key, PROPERTY_NUMBER))
            mm_common_connect_properties_set_number (properties, value);
        else if (g_str_equal (key, PROPERTY_ALLOWED_BANDS)) {
            MMModemBand *bands = NULL;
            guint n_bands = 0;

            mm_common_get_bands_from_string (value, &bands, &n_bands, &inner_error);
            if (!inner_error)
                mm_common_connect_properties_set_allowed_bands (properties, bands, n_bands);
            g_free (bands);
        } else if (g_str_equal (key, PROPERTY_ALLOWED_MODES)) {
            allowed_modes_str = value;
        } else if (g_str_equal (key, PROPERTY_PREFERRED_MODE)) {
            preferred_mode_str = value;
        } else {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_INVALID_ARGS,
                                       "Invalid properties string, unexpected key '%s'",
                                       key);
            break;
        }

        key = words[++i];
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (properties);
        properties = NULL;
    }
    else if (allowed_modes_str || preferred_mode_str) {
        MMModemMode allowed_modes;
        MMModemMode preferred_mode;

        allowed_modes = (allowed_modes_str ?
                         mm_common_get_modes_from_string (allowed_modes_str,
                                                          &inner_error) :
                         MM_MODEM_MODE_ANY);
        if (!inner_error) {
            preferred_mode = (preferred_mode_str ?
                              mm_common_get_modes_from_string (preferred_mode_str,
                                                               &inner_error) :
                              MM_MODEM_MODE_NONE);
        }

        if (inner_error) {
            g_propagate_error (error, inner_error);
            g_object_unref (properties);
            properties = NULL;
        } else {
            mm_common_connect_properties_set_allowed_modes (
                properties,
                allowed_modes,
                preferred_mode);
        }
    }

    g_strfreev (words);
    return properties;
}

/*****************************************************************************/

MMCommonConnectProperties *
mm_common_connect_properties_new_from_dictionary (GVariant *dictionary,
                                                  GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMCommonConnectProperties *properties;
    GVariant *allowed_modes_variant = NULL;
    GVariant *preferred_mode_variant = NULL;

    properties = mm_common_connect_properties_new ();
    if (!dictionary)
        return properties;

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        if (g_str_equal (key, PROPERTY_PIN))
            mm_common_connect_properties_set_pin (
                properties,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_OPERATOR_ID))
            mm_common_connect_properties_set_operator_id (
                properties,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_ALLOWED_BANDS)) {
            GArray *array;

            array = mm_common_bands_variant_to_garray (value);
            mm_common_connect_properties_set_allowed_bands (
                properties,
                (MMModemBand *)array->data,
                array->len);
            g_array_unref (array);
        } else if (g_str_equal (key, PROPERTY_ALLOWED_MODES))
            allowed_modes_variant = g_variant_ref (value);
        else if (g_str_equal (key, PROPERTY_PREFERRED_MODE))
            preferred_mode_variant = g_variant_ref (value);
        else if (g_str_equal (key, PROPERTY_APN))
            mm_common_connect_properties_set_apn (
                properties,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_USER))
            mm_common_connect_properties_set_user (
                properties,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_PASSWORD))
            mm_common_connect_properties_set_password (
                properties,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_IP_TYPE))
            mm_common_connect_properties_set_ip_type (
                properties,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_NUMBER))
            mm_common_connect_properties_set_number (
                properties,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_ALLOW_ROAMING))
            mm_common_connect_properties_set_allow_roaming (
                properties,
                g_variant_get_boolean (value));
        else {
            /* Set inner error, will stop the loop */
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_INVALID_ARGS,
                                       "Invalid properties dictionary, unexpected key '%s'",
                                       key);
        }

        g_free (key);
        g_variant_unref (value);
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (properties);
        properties = NULL;
    }
    /* If we got allowed modes variant, check if we got preferred mode */
    else if (allowed_modes_variant) {
        mm_common_connect_properties_set_allowed_modes (
            properties,
            g_variant_get_uint32 (allowed_modes_variant),
            (preferred_mode_variant ?
             g_variant_get_uint32 (preferred_mode_variant) :
             MM_MODEM_MODE_NONE));
    }
    /* If we only got preferred mode, assume allowed is ANY */
    else if (preferred_mode_variant) {
        mm_common_connect_properties_set_allowed_modes (
            properties,
            MM_MODEM_MODE_ANY,
            g_variant_get_uint32 (preferred_mode_variant));
    }

    /* Cleanup last things before exiting */
    if (allowed_modes_variant)
        g_variant_unref (allowed_modes_variant);
    if (preferred_mode_variant)
        g_variant_unref (preferred_mode_variant);

    return properties;
}

/*****************************************************************************/

MMCommonConnectProperties *
mm_common_connect_properties_new (void)
{
    return (MM_COMMON_CONNECT_PROPERTIES (
                g_object_new (MM_TYPE_COMMON_CONNECT_PROPERTIES, NULL)));
}

static void
mm_common_connect_properties_init (MMCommonConnectProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_COMMON_CONNECT_PROPERTIES,
                                              MMCommonConnectPropertiesPrivate);

    /* Some defaults */
    self->priv->allow_roaming = TRUE;
    self->priv->allowed_modes = MM_MODEM_MODE_ANY;
    self->priv->preferred_mode = MM_MODEM_MODE_NONE;
    self->priv->allowed_bands = g_new (MMModemBand, 1);
    self->priv->allowed_bands[0] = MM_MODEM_BAND_ANY;
    self->priv->n_allowed_bands = 1;
}

static void
finalize (GObject *object)
{
    MMCommonConnectProperties *self = MM_COMMON_CONNECT_PROPERTIES (object);

    g_free (self->priv->pin);
    g_free (self->priv->operator_id);
    g_free (self->priv->allowed_bands);
    g_free (self->priv->apn);
    g_free (self->priv->user);
    g_free (self->priv->password);
    g_free (self->priv->ip_type);
    g_free (self->priv->number);

    G_OBJECT_CLASS (mm_common_connect_properties_parent_class)->finalize (object);
}

static void
mm_common_connect_properties_class_init (MMCommonConnectPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCommonConnectPropertiesPrivate));

    object_class->finalize = finalize;
}
