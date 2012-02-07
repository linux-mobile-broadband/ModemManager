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
#include <errno.h>
#include <stdlib.h>

#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-common-sms-properties.h"

G_DEFINE_TYPE (MMCommonSmsProperties, mm_common_sms_properties, G_TYPE_OBJECT);

#define PROPERTY_TEXT      "text"
#define PROPERTY_NUMBER    "number"
#define PROPERTY_SMSC      "smsc"
#define PROPERTY_VALIDITY  "validity"
#define PROPERTY_CLASS     "class"

struct _MMCommonSmsPropertiesPrivate {
    gchar *text;
    gchar *number;
    gchar *smsc;
    gboolean validity_set;
    guint validity;
    gboolean class_set;
    guint class;
};

/*****************************************************************************/

void
mm_common_sms_properties_set_text (MMCommonSmsProperties *self,
                                   const gchar *text)
{
    g_free (self->priv->text);
    self->priv->text = g_strdup (text);
}

void
mm_common_sms_properties_set_number (MMCommonSmsProperties *self,
                                     const gchar *number)
{
    g_free (self->priv->number);
    self->priv->number = g_strdup (number);
}

void
mm_common_sms_properties_set_smsc (MMCommonSmsProperties *self,
                                     const gchar *smsc)
{
    g_free (self->priv->smsc);
    self->priv->smsc = g_strdup (smsc);
}

void
mm_common_sms_properties_set_validity (MMCommonSmsProperties *self,
                                       guint validity)
{
    self->priv->validity_set = TRUE;
    self->priv->validity = validity;
}

void
mm_common_sms_properties_set_class (MMCommonSmsProperties *self,
                                    guint class)
{
    self->priv->class_set = TRUE;
    self->priv->class = class;
}

/*****************************************************************************/

const gchar *
mm_common_sms_properties_get_text (MMCommonSmsProperties *self)
{
    return self->priv->text;
}

const gchar *
mm_common_sms_properties_get_number (MMCommonSmsProperties *self)
{
    return self->priv->number;
}

const gchar *
mm_common_sms_properties_get_smsc (MMCommonSmsProperties *self)
{
    return self->priv->smsc;
}

guint
mm_common_sms_properties_get_validity (MMCommonSmsProperties *self)
{
    return self->priv->validity;
}

guint
mm_common_sms_properties_get_class (MMCommonSmsProperties *self)
{
    return self->priv->class;
}

/*****************************************************************************/

GVariant *
mm_common_sms_properties_get_dictionary (MMCommonSmsProperties *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->text)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_TEXT,
                               g_variant_new_string (self->priv->text));

    if (self->priv->number)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_NUMBER,
                               g_variant_new_string (self->priv->number));

    if (self->priv->smsc)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_SMSC,
                               g_variant_new_string (self->priv->smsc));

    if (self->priv->validity_set)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_VALIDITY,
                               g_variant_new_uint32 (self->priv->validity));

    if (self->priv->class_set)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_CLASS,
                               g_variant_new_uint32 (self->priv->class));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

static guint
parse_uint (const gchar *str,
            GError **error)
{
    guint num;

    errno = 0;
    num = strtoul (str, NULL, 10);
    if ((num < G_MAXUINT32) && (errno == 0))
        return num;

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Invalid properties string, cannot parset '%s' as uint",
                 str);
    return 0;
}

static gboolean
consume_string (MMCommonSmsProperties *self,
                const gchar *key,
                const gchar *value,
                GError **error)
{
    if (g_str_equal (key, PROPERTY_TEXT))
        mm_common_sms_properties_set_text (self, value);
    else if (g_str_equal (key, PROPERTY_NUMBER))
        mm_common_sms_properties_set_number (self, value);
    else if (g_str_equal (key, PROPERTY_SMSC))
        mm_common_sms_properties_set_smsc (self, value);
    else if (g_str_equal (key, PROPERTY_VALIDITY)) {
        GError *inner_error = NULL;
        guint n;

        n = parse_uint (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }

        mm_common_sms_properties_set_validity (self, n);
    } else if (g_str_equal (key, PROPERTY_CLASS)) {
        GError *inner_error = NULL;
        guint n;

        n = parse_uint (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }

        mm_common_sms_properties_set_class (self, n);
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

MMCommonSmsProperties *
mm_common_sms_properties_new_from_string (const gchar *str,
                                          GError **error)
{
    GError *inner_error = NULL;
    MMCommonSmsProperties *properties;
    gchar **words;
    gchar *key;
    gchar *value;
    guint i;

    properties = mm_common_sms_properties_new ();

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

        if (!consume_string (properties,
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

static gboolean
consume_variant (MMCommonSmsProperties *properties,
                 const gchar *key,
                 GVariant *value,
                 GError **error)
{
    if (g_str_equal (key, PROPERTY_TEXT))
        mm_common_sms_properties_set_text (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_NUMBER))
        mm_common_sms_properties_set_number (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_SMSC))
        mm_common_sms_properties_set_smsc (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_VALIDITY))
        mm_common_sms_properties_set_validity (
            properties,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_CLASS))
        mm_common_sms_properties_set_class (
            properties,
            g_variant_get_uint32 (value));
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

MMCommonSmsProperties *
mm_common_sms_properties_new_from_dictionary (GVariant *dictionary,
                                              GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMCommonSmsProperties *properties;

    properties = mm_common_sms_properties_new ();
    if (!dictionary)
        return properties;

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        consume_variant (properties,
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

MMCommonSmsProperties *
mm_common_sms_properties_dup (MMCommonSmsProperties *orig)
{
    GVariant *dict;
    MMCommonSmsProperties *copy;
    GError *error = NULL;

    dict = mm_common_sms_properties_get_dictionary (orig);
    copy = mm_common_sms_properties_new_from_dictionary (dict, &error);
    g_assert_no_error (error);
    g_variant_unref (dict);

    return copy;
}

/*****************************************************************************/

MMCommonSmsProperties *
mm_common_sms_properties_new (void)
{
    return (MM_COMMON_SMS_PROPERTIES (
                g_object_new (MM_TYPE_COMMON_SMS_PROPERTIES, NULL)));
}

static void
mm_common_sms_properties_init (MMCommonSmsProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_COMMON_SMS_PROPERTIES,
                                              MMCommonSmsPropertiesPrivate);
}

static void
finalize (GObject *object)
{
    MMCommonSmsProperties *self = MM_COMMON_SMS_PROPERTIES (object);

    g_free (self->priv->text);
    g_free (self->priv->number);
    g_free (self->priv->smsc);

    G_OBJECT_CLASS (mm_common_sms_properties_parent_class)->finalize (object);
}

static void
mm_common_sms_properties_class_init (MMCommonSmsPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCommonSmsPropertiesPrivate));

    object_class->finalize = finalize;
}
