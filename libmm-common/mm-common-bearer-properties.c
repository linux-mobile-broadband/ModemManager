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

#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-common-bearer-properties.h"

G_DEFINE_TYPE (MMCommonBearerProperties, mm_common_bearer_properties, G_TYPE_OBJECT);

#define PROPERTY_APN             "apn"
#define PROPERTY_USER            "user"
#define PROPERTY_PASSWORD        "password"
#define PROPERTY_IP_TYPE         "ip-type"
#define PROPERTY_NUMBER          "number"
#define PROPERTY_ALLOW_ROAMING   "allow-roaming"
#define PROPERTY_RM_PROTOCOL     "rm-protocol"

struct _MMCommonBearerPropertiesPrivate {
    /* APN */
    gchar *apn;
    /* IP type */
    gchar *ip_type;
    /* Number */
    gchar *number;
    /* User */
    gchar *user;
    /* Password */
    gchar *password;
    /* Roaming allowance */
    gboolean allow_roaming_set;
    gboolean allow_roaming;
    /* Protocol of the Rm interface */
    MMModemCdmaRmProtocol rm_protocol;
};

/*****************************************************************************/

void
mm_common_bearer_properties_set_apn (MMCommonBearerProperties *self,
                                     const gchar *apn)
{
    g_free (self->priv->apn);
    self->priv->apn = g_strdup (apn);
}

void
mm_common_bearer_properties_set_user (MMCommonBearerProperties *self,
                                      const gchar *user)
{
    g_free (self->priv->user);
    self->priv->user = g_strdup (user);
}

void
mm_common_bearer_properties_set_password (MMCommonBearerProperties *self,
                                          const gchar *password)
{
    g_free (self->priv->password);
    self->priv->password = g_strdup (password);
}

void
mm_common_bearer_properties_set_ip_type (MMCommonBearerProperties *self,
                                         const gchar *ip_type)
{
    g_free (self->priv->ip_type);
    self->priv->ip_type = g_strdup (ip_type);
}

void
mm_common_bearer_properties_set_allow_roaming (MMCommonBearerProperties *self,
                                               gboolean allow_roaming)
{
    self->priv->allow_roaming = allow_roaming;
    self->priv->allow_roaming_set = TRUE;
}

void
mm_common_bearer_properties_set_number (MMCommonBearerProperties *self,
                                        const gchar *number)
{
    g_free (self->priv->number);
    self->priv->number = g_strdup (number);
}

void
mm_common_bearer_properties_set_rm_protocol (MMCommonBearerProperties *self,
                                             MMModemCdmaRmProtocol protocol)
{
    self->priv->rm_protocol = protocol;
}

/*****************************************************************************/

const gchar *
mm_common_bearer_properties_get_apn (MMCommonBearerProperties *self)
{
    return self->priv->apn;
}

const gchar *
mm_common_bearer_properties_get_user (MMCommonBearerProperties *self)
{
    return self->priv->user;
}

const gchar *
mm_common_bearer_properties_get_password (MMCommonBearerProperties *self)
{
    return self->priv->password;
}

const gchar *
mm_common_bearer_properties_get_ip_type (MMCommonBearerProperties *self)
{
    return self->priv->ip_type;
}

gboolean
mm_common_bearer_properties_get_allow_roaming (MMCommonBearerProperties *self)
{
    return self->priv->allow_roaming;
}

const gchar *
mm_common_bearer_properties_get_number (MMCommonBearerProperties *self)
{
    return self->priv->number;
}

MMModemCdmaRmProtocol
mm_common_bearer_properties_get_rm_protocol (MMCommonBearerProperties *self)
{
    return self->priv->rm_protocol;
}

/*****************************************************************************/

GVariant *
mm_common_bearer_properties_get_dictionary (MMCommonBearerProperties *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

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

    if (self->priv->rm_protocol)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_RM_PROTOCOL,
                               g_variant_new_uint32 (self->priv->rm_protocol));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

gboolean
mm_common_bearer_properties_consume_string (MMCommonBearerProperties *self,
                                            const gchar *key,
                                            const gchar *value,
                                            GError **error)
{
    if (g_str_equal (key, PROPERTY_APN))
        mm_common_bearer_properties_set_apn (self, value);
    else if (g_str_equal (key, PROPERTY_USER))
        mm_common_bearer_properties_set_user (self, value);
    else if (g_str_equal (key, PROPERTY_PASSWORD))
        mm_common_bearer_properties_set_password (self, value);
    else if (g_str_equal (key, PROPERTY_IP_TYPE))
        mm_common_bearer_properties_set_ip_type (self, value);
    else if (g_str_equal (key, PROPERTY_ALLOW_ROAMING)) {
        GError *inner_error = NULL;
        gboolean allow_roaming;

        allow_roaming = mm_common_get_boolean_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_common_bearer_properties_set_allow_roaming (self, allow_roaming);
    } else if (g_str_equal (key, PROPERTY_NUMBER))
        mm_common_bearer_properties_set_number (self, value);
    else if (g_str_equal (key, PROPERTY_RM_PROTOCOL)) {
        GError *inner_error = NULL;
        MMModemCdmaRmProtocol protocol;

        protocol = mm_common_get_rm_protocol_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_common_bearer_properties_set_rm_protocol (self, protocol);
    } else {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid properties string, unexpected key '%s'",
                     key);
        return FALSE;
    }

    return TRUE;
}

MMCommonBearerProperties *
mm_common_bearer_properties_new_from_string (const gchar *str,
                                             GError **error)
{
    GError *inner_error = NULL;
    MMCommonBearerProperties *properties;
    gchar **words;
    gchar *key;
    gchar *value;
    guint i;

    properties = mm_common_bearer_properties_new ();

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

        if (!mm_common_bearer_properties_consume_string (properties,
                                                         key,
                                                         value,
                                                         &inner_error))
            break;

        key = words[++i];
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (properties);
        properties = NULL;
    }

    g_strfreev (words);
    return properties;
}

/*****************************************************************************/

gboolean
mm_common_bearer_properties_consume_variant (MMCommonBearerProperties *properties,
                                             const gchar *key,
                                             GVariant *value,
                                             GError **error)
{
    if (g_str_equal (key, PROPERTY_APN))
        mm_common_bearer_properties_set_apn (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_USER))
        mm_common_bearer_properties_set_user (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_PASSWORD))
        mm_common_bearer_properties_set_password (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_IP_TYPE))
        mm_common_bearer_properties_set_ip_type (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_NUMBER))
        mm_common_bearer_properties_set_number (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_ALLOW_ROAMING))
        mm_common_bearer_properties_set_allow_roaming (
            properties,
            g_variant_get_boolean (value));
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

MMCommonBearerProperties *
mm_common_bearer_properties_new_from_dictionary (GVariant *dictionary,
                                                 GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMCommonBearerProperties *properties;

    properties = mm_common_bearer_properties_new ();
    if (!dictionary)
        return properties;

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        mm_common_bearer_properties_consume_variant (properties,
                                                     key,
                                                     value,
                                                     &inner_error);
        g_free (key);
        g_variant_unref (value);
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (properties);
        properties = NULL;
    }

    return properties;
}

/*****************************************************************************/

MMCommonBearerProperties *
mm_common_bearer_properties_new (void)
{
    return (MM_COMMON_BEARER_PROPERTIES (
                g_object_new (MM_TYPE_COMMON_BEARER_PROPERTIES, NULL)));
}

static void
mm_common_bearer_properties_init (MMCommonBearerProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_COMMON_BEARER_PROPERTIES,
                                              MMCommonBearerPropertiesPrivate);

    /* Some defaults */
    self->priv->allow_roaming = TRUE;
    self->priv->rm_protocol = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMCommonBearerProperties *self = MM_COMMON_BEARER_PROPERTIES (object);

    g_free (self->priv->apn);
    g_free (self->priv->user);
    g_free (self->priv->password);
    g_free (self->priv->ip_type);
    g_free (self->priv->number);

    G_OBJECT_CLASS (mm_common_bearer_properties_parent_class)->finalize (object);
}

static void
mm_common_bearer_properties_class_init (MMCommonBearerPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCommonBearerPropertiesPrivate));

    object_class->finalize = finalize;
}
