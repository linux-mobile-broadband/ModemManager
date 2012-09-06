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
#include "mm-sms-properties.h"

G_DEFINE_TYPE (MMSmsProperties, mm_sms_properties, G_TYPE_OBJECT);

#define PROPERTY_TEXT      "text"
#define PROPERTY_DATA      "data"
#define PROPERTY_NUMBER    "number"
#define PROPERTY_SMSC      "smsc"
#define PROPERTY_VALIDITY  "validity"
#define PROPERTY_CLASS     "class"

struct _MMSmsPropertiesPrivate {
    gchar *text;
    GByteArray *data;
    gchar *number;
    gchar *smsc;
    gboolean validity_set;
    guint validity;
    gboolean class_set;
    guint class;
};

/*****************************************************************************/

void
mm_sms_properties_set_text (MMSmsProperties *self,
                            const gchar *text)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    g_free (self->priv->text);
    self->priv->text = g_strdup (text);
}

void
mm_sms_properties_set_data (MMSmsProperties *self,
                            const guint8 *data,
                            gsize data_length)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    if (self->priv->data)
        g_byte_array_unref (self->priv->data);

    if (data && data_length)
        self->priv->data = g_byte_array_append (g_byte_array_sized_new (data_length),
                                                data,
                                                data_length);
    else
        self->priv->data = NULL;
}

void
mm_sms_properties_set_data_bytearray (MMSmsProperties *self,
                                      GByteArray *data)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    if (self->priv->data)
        g_byte_array_unref (self->priv->data);

    self->priv->data = (data ? g_byte_array_ref (data) : NULL);
}

void
mm_sms_properties_set_number (MMSmsProperties *self,
                              const gchar *number)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    g_free (self->priv->number);
    self->priv->number = g_strdup (number);
}

void
mm_sms_properties_set_smsc (MMSmsProperties *self,
                            const gchar *smsc)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    g_free (self->priv->smsc);
    self->priv->smsc = g_strdup (smsc);
}

void
mm_sms_properties_set_validity (MMSmsProperties *self,
                                guint validity)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    self->priv->validity_set = TRUE;
    self->priv->validity = validity;
}

void
mm_sms_properties_set_class (MMSmsProperties *self,
                             guint class)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    self->priv->class_set = TRUE;
    self->priv->class = class;
}

/*****************************************************************************/

const gchar *
mm_sms_properties_get_text (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    return self->priv->text;
}

const gchar *
mm_sms_properties_get_number (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    return self->priv->number;
}

const guint8 *
mm_sms_properties_get_data (MMSmsProperties *self,
                            gsize *data_len)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    if (self->priv->data && data_len)
        *data_len = self->priv->data->len;

    return self->priv->data->data;
}

GByteArray *
mm_sms_properties_peek_data_bytearray (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    return self->priv->data;
}

GByteArray *
mm_sms_properties_get_data_bytearray (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    return (self->priv->data ? g_byte_array_ref (self->priv->data) : NULL);
}

const gchar *
mm_sms_properties_get_smsc (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    return self->priv->smsc;
}

guint
mm_sms_properties_get_validity (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), 0);

    return self->priv->validity;
}

guint
mm_sms_properties_get_class (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), 0);

    return self->priv->class;
}

/*****************************************************************************/

GVariant *
mm_sms_properties_get_dictionary (MMSmsProperties *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->text)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_TEXT,
                               g_variant_new_string (self->priv->text));

    if (self->priv->data)
        g_variant_builder_add (
            &builder,
            "{sv}",
            PROPERTY_DATA,
            g_variant_new_from_data (G_VARIANT_TYPE ("ay"),
                                     self->priv->data->data,
                                     self->priv->data->len * sizeof (guint8),
                                     TRUE,
                                     NULL,
                                     NULL));

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
                 "Invalid properties string, cannot parse '%s' as uint",
                 str);
    return 0;
}

static gboolean
consume_string (MMSmsProperties *self,
                const gchar *key,
                const gchar *value,
                GError **error)
{
    if (g_str_equal (key, PROPERTY_TEXT))
        mm_sms_properties_set_text (self, value);
    else if (g_str_equal (key, PROPERTY_NUMBER))
        mm_sms_properties_set_number (self, value);
    else if (g_str_equal (key, PROPERTY_SMSC))
        mm_sms_properties_set_smsc (self, value);
    else if (g_str_equal (key, PROPERTY_VALIDITY)) {
        GError *inner_error = NULL;
        guint n;

        n = parse_uint (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }

        mm_sms_properties_set_validity (self, n);
    } else if (g_str_equal (key, PROPERTY_CLASS)) {
        GError *inner_error = NULL;
        guint n;

        n = parse_uint (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }

        mm_sms_properties_set_class (self, n);
    } else if (g_str_equal (key, PROPERTY_DATA)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid properties string, key '%s' cannot be given in a string",
                     key);
        return FALSE;
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

typedef struct {
    MMSmsProperties *properties;
    GError *error;
} ParseKeyValueContext;

static gboolean
key_value_foreach (const gchar *key,
                   const gchar *value,
                   ParseKeyValueContext *ctx)
{
    return consume_string (ctx->properties,
                           key,
                           value,
                           &ctx->error);
}

MMSmsProperties *
mm_sms_properties_new_from_string (const gchar *str,
                                   GError **error)
{
    ParseKeyValueContext ctx;

    ctx.properties = mm_sms_properties_new ();
    ctx.error = NULL;

    mm_common_parse_key_value_string (str,
                                      &ctx.error,
                                      (MMParseKeyValueForeachFn)key_value_foreach,
                                      &ctx);

    /* If error, destroy the object */
    if (ctx.error) {
        g_propagate_error (error, ctx.error);
        g_object_unref (ctx.properties);
        ctx.properties = NULL;
    }

    return ctx.properties;
}

/*****************************************************************************/

static gboolean
consume_variant (MMSmsProperties *properties,
                 const gchar *key,
                 GVariant *value,
                 GError **error)
{
    if (g_str_equal (key, PROPERTY_TEXT))
        mm_sms_properties_set_text (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_DATA)) {
        const guint8 *data;
        gsize data_len = 0;

        data = g_variant_get_fixed_array (value, &data_len, sizeof (guint8));
        mm_sms_properties_set_data (
            properties,
            data,
            data_len);
    } else if (g_str_equal (key, PROPERTY_NUMBER))
        mm_sms_properties_set_number (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_SMSC))
        mm_sms_properties_set_smsc (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_VALIDITY))
        mm_sms_properties_set_validity (
            properties,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_CLASS))
        mm_sms_properties_set_class (
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

MMSmsProperties *
mm_sms_properties_new_from_dictionary (GVariant *dictionary,
                                       GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMSmsProperties *properties;

    properties = mm_sms_properties_new ();
    if (!dictionary)
        return properties;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create SMS properties from dictionary: "
                     "invalid variant type received");
        g_object_unref (properties);
        return NULL;
    }

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

MMSmsProperties *
mm_sms_properties_dup (MMSmsProperties *orig)
{
    GVariant *dict;
    MMSmsProperties *copy;
    GError *error = NULL;

    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (orig), NULL);

    dict = mm_sms_properties_get_dictionary (orig);
    copy = mm_sms_properties_new_from_dictionary (dict, &error);
    g_assert_no_error (error);
    g_variant_unref (dict);

    return copy;
}

/*****************************************************************************/

MMSmsProperties *
mm_sms_properties_new (void)
{
    return (MM_SMS_PROPERTIES (g_object_new (MM_TYPE_SMS_PROPERTIES, NULL)));
}

static void
mm_sms_properties_init (MMSmsProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_SMS_PROPERTIES,
                                              MMSmsPropertiesPrivate);
}

static void
finalize (GObject *object)
{
    MMSmsProperties *self = MM_SMS_PROPERTIES (object);

    g_free (self->priv->text);
    g_free (self->priv->number);
    g_free (self->priv->smsc);
    if (self->priv->data)
        g_byte_array_unref (self->priv->data);

    G_OBJECT_CLASS (mm_sms_properties_parent_class)->finalize (object);
}

static void
mm_sms_properties_class_init (MMSmsPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSmsPropertiesPrivate));

    object_class->finalize = finalize;
}
