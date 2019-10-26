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
#include "mm-enums-types.h"
#include "mm-common-helpers.h"
#include "mm-sms-properties.h"

/**
 * SECTION: mm-sms-properties
 * @title: MMSmsProperties
 * @short_description: Helper object to handle SMS properties.
 *
 * The #MMSmsProperties is an object handling the properties to be set
 * in newly created SMS objects.
 *
 * This object is created by the user and passed to ModemManager with either
 * mm_modem_messaging_create() or mm_modem_messaging_create_sync().
 */

G_DEFINE_TYPE (MMSmsProperties, mm_sms_properties, G_TYPE_OBJECT)

#define PROPERTY_TEXT                    "text"
#define PROPERTY_DATA                    "data"
#define PROPERTY_NUMBER                  "number"
#define PROPERTY_SMSC                    "smsc"
#define PROPERTY_VALIDITY                "validity"
#define PROPERTY_CLASS                   "class"
#define PROPERTY_DELIVERY_REPORT_REQUEST "delivery-report-request"
#define PROPERTY_TELESERVICE_ID          "teleservice-id"
#define PROPERTY_SERVICE_CATEGORY        "service-category"

struct _MMSmsPropertiesPrivate {
    gchar *text;
    GByteArray *data;
    gchar *number;
    gchar *smsc;
    MMSmsValidityType validity_type;
    guint validity_relative;
    gint class;
    gboolean delivery_report_request_set;
    gboolean delivery_report_request;
    MMSmsCdmaTeleserviceId teleservice_id;
    MMSmsCdmaServiceCategory service_category;
};

/*****************************************************************************/

/**
 * mm_sms_properties_set_text:
 * @self: A #MMSmsProperties.
 * @text: The text to set, in UTF-8.
 *
 * Sets the message text.
 *
 * Since: 1.0
 */
void
mm_sms_properties_set_text (MMSmsProperties *self,
                            const gchar *text)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    g_free (self->priv->text);
    self->priv->text = g_strdup (text);
}

/**
 * mm_sms_properties_get_text:
 * @self: A #MMSmsProperties.
 *
 * Gets the message text, in UTF-8.
 *
 * Returns: (transfer none): The message text, or %NULL if it doesn't contain
 * any (e.g. contains data instead). Do not free the returned value, it is
 * owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_sms_properties_get_text (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    return self->priv->text;
}

/*****************************************************************************/

/**
 * mm_sms_properties_set_data:
 * @self: A #MMSmsProperties.
 * @data: The data to set.
 * @data_length: Length of @data.
 *
 * Sets the message data.
 *
 * Since: 1.0
 */
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

/**
 * mm_sms_properties_set_data_bytearray:
 * @self: A #MMSmsProperties.
 * @data: A #GByteArray with the data to set. This method takes a new reference
 *  of @data.
 *
 * Sets the message data.
 *
 * Since: 1.0
 */
void
mm_sms_properties_set_data_bytearray (MMSmsProperties *self,
                                      GByteArray *data)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    if (self->priv->data)
        g_byte_array_unref (self->priv->data);

    self->priv->data = (data ? g_byte_array_ref (data) : NULL);
}

/**
 * mm_sms_properties_get_data:
 * @self: A #MMSmsProperties.
 * @data_len: (out): Size of the output data, if any given.
 *
 * Gets the message data.
 *
 * Returns: (transfer none): The message data, or %NULL if it doesn't contain
 * any (e.g. contains text instead).
 *
 * Since: 1.0
 */
const guint8 *
mm_sms_properties_get_data (MMSmsProperties *self,
                            gsize *data_len)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    if (data_len)
        *data_len = (self->priv->data ? self->priv->data->len : 0);

    return (self->priv->data ? self->priv->data->data : NULL);
}

/**
 * mm_sms_properties_peek_data_bytearray:
 * @self: A #MMSmsProperties.
 *
 * Gets the message data.
 *
 * Returns: (transfer none): A #GByteArray with the message data, or %NULL if it
 * doesn't contain any (e.g. contains text instead). Do not free the returned
 * value, it is owned by @self.
 *
 * Since: 1.0
 */
GByteArray *
mm_sms_properties_peek_data_bytearray (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    return self->priv->data;
}

/**
 * mm_sms_properties_get_data_bytearray:
 * @self: A #MMSmsProperties.
 *
 * Gets the message data.
 *
 * Returns: (transfer full): A #GByteArray with the message data, or %NULL if it
 * doesn't contain any (e.g. contains text instead). The returned value should
 * be freed with g_byte_array_unref().
 *
 * Since: 1.0
 */
GByteArray *
mm_sms_properties_get_data_bytearray (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    return (self->priv->data ? g_byte_array_ref (self->priv->data) : NULL);
}

/*****************************************************************************/

/**
 * mm_sms_properties_set_number:
 * @self: A #MMSmsProperties.
 * @number: The number.
 *
 * Sets the number to which the message is addressed.
 *
 * Since: 1.0
 */
void
mm_sms_properties_set_number (MMSmsProperties *self,
                              const gchar *number)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    g_free (self->priv->number);
    self->priv->number = g_strdup (number);
}

/**
 * mm_sms_properties_get_number:
 * @self: A #MMSmsProperties.
 *
 * Gets the number to which the message is addressed.
 *
 * Returns: (transfer none): The number, or %NULL if it couldn't be retrieved.
 * Do not free the returned value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_sms_properties_get_number (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    return self->priv->number;
}

/*****************************************************************************/

/**
 * mm_sms_properties_set_smsc:
 * @self: A #MMSmsProperties.
 * @smsc: The SMSC number.
 *
 * Sets the SMS service center number.
 *
 * Since: 1.0
 */
void
mm_sms_properties_set_smsc (MMSmsProperties *self,
                            const gchar *smsc)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    g_free (self->priv->smsc);
    self->priv->smsc = g_strdup (smsc);
}

/**
 * mm_sms_properties_get_smsc:
 * @self: A #MMSmsProperties.
 *
 * Gets the SMS service center number.
 *
 * Returns: (transfer none): The number of the SMSC, or %NULL if it couldn't be
 * retrieved. Do not free the returned value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_sms_properties_get_smsc (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), NULL);

    return self->priv->smsc;
}

/*****************************************************************************/

/**
 * mm_sms_properties_set_validity_relative:
 * @self: A #MMSmsProperties.
 * @validity: The validity of %MM_SMS_VALIDITY_TYPE_RELATIVE type.
 *
 * Sets the relative validity time of the SMS.
 *
 * Since: 1.0
 */
void
mm_sms_properties_set_validity_relative (MMSmsProperties *self,
                                         guint validity)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    self->priv->validity_type = MM_SMS_VALIDITY_TYPE_RELATIVE;
    self->priv->validity_relative = validity;
}

/**
 * mm_sms_properties_get_validity_type:
 * @self: A #MMSmsProperties.
 *
 * Gets the relative validity type the SMS.
 *
 * Returns: a #MMSmsValidityType.
 *
 * Since: 1.0
 */
MMSmsValidityType
mm_sms_properties_get_validity_type (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), 0);

    return self->priv->validity_type;
}

/**
 * mm_sms_properties_get_validity_relative:
 * @self: A #MMSmsProperties.
 *
 * Gets the relative validity time of the SMS.
 *
 * Returns: the validity time or 0 if unknown.
 *
 * Since: 1.0
 */
guint
mm_sms_properties_get_validity_relative (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), 0);
    g_return_val_if_fail (self->priv->validity_type == MM_SMS_VALIDITY_TYPE_RELATIVE, 0);

    return self->priv->validity_relative;
}

/*****************************************************************************/

/**
 * mm_sms_properties_set_class:
 * @self: A #MMSmsProperties.
 * @message_class: The message class (0..3), or -1 for invalid/unset class.
 *
 * Sets the 3GPP message class of the SMS.
 *
 * Since: 1.0
 */
void
mm_sms_properties_set_class (MMSmsProperties *self,
                             gint message_class)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));
    g_return_if_fail (message_class >= -1 && message_class <= 3);

    self->priv->class = message_class;
}

/**
 * mm_sms_properties_get_class:
 * @self: A #MMSmsProperties.
 *
 * Gets the 3GPP message class of the SMS.
 *
 * Returns: the message class, or -1 for invalid/unset class.
 *
 * Since: 1.0
 */
gint
mm_sms_properties_get_class (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), -1);

    return self->priv->class;
}

/*****************************************************************************/

/**
 * mm_sms_properties_set_delivery_report_request:
 * @self: A #MMSmsProperties.
 * @request: %TRUE if delivery report is requested, %FALSE otherwise.
 *
 * Sets whether delivery report is requested for the SMS.
 *
 * Since: 1.0
 */
void
mm_sms_properties_set_delivery_report_request (MMSmsProperties *self,
                                               gboolean request)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    self->priv->delivery_report_request_set = TRUE;
    self->priv->delivery_report_request = request;
}

/**
 * mm_sms_properties_get_delivery_report_request:
 * @self: A #MMSmsProperties.
 *
 * Checks whether delivery report is requested for the SMS.
 *
 * Returns: %TRUE if delivery report is requested, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_sms_properties_get_delivery_report_request (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), FALSE);

    return self->priv->delivery_report_request;
}

/*****************************************************************************/

/**
 * mm_sms_properties_set_teleservice_id:
 * @self: A #MMSmsProperties.
 * @teleservice_id: The CDMA teleservice ID.
 *
 * Sets the CDMA teleservice ID of the SMS.
 *
 * Since: 1.2
 */
void
mm_sms_properties_set_teleservice_id (MMSmsProperties *self,
                                      MMSmsCdmaTeleserviceId teleservice_id)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    self->priv->teleservice_id = teleservice_id;
}

/**
 * mm_sms_properties_get_teleservice_id:
 * @self: A #MMSmsProperties.
 *
 * Gets the CDMA teleservice ID of the SMS.
 *
 * Returns: the CDMA teleservice ID.
 *
 * Since: 1.2
 */
MMSmsCdmaTeleserviceId
mm_sms_properties_get_teleservice_id (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN);

    return self->priv->teleservice_id;
}

/*****************************************************************************/

/**
 * mm_sms_properties_set_service_category:
 * @self: A #MMSmsProperties.
 * @service_category: The CDMA service category.
 *
 * Sets the CDMA service category of the SMS.
 *
 * Since: 1.2
 */
void
mm_sms_properties_set_service_category (MMSmsProperties *self,
                                        MMSmsCdmaServiceCategory service_category)
{
    g_return_if_fail (MM_IS_SMS_PROPERTIES (self));

    self->priv->service_category = service_category;
}

/**
 * mm_sms_properties_get_service_category:
 * @self: A #MMSmsProperties.
 *
 * Gets the CDMA message service category of the SMS.
 *
 * Returns: the CDMA service category.
 *
 * Since: 1.2
 */
MMSmsCdmaServiceCategory
mm_sms_properties_get_service_category (MMSmsProperties *self)
{
    g_return_val_if_fail (MM_IS_SMS_PROPERTIES (self), MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN);

    return self->priv->service_category;
}

/*****************************************************************************/

/**
 * mm_sms_properties_get_dictionary: (skip)
 */
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

    if (self->priv->validity_type == MM_SMS_VALIDITY_TYPE_RELATIVE)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_VALIDITY,
                               g_variant_new ("(uv)", MM_SMS_VALIDITY_TYPE_RELATIVE, g_variant_new_uint32 (self->priv->validity_relative)));

    if (self->priv->class >= 0)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_CLASS,
                               g_variant_new_int32 (self->priv->class));

    if (self->priv->delivery_report_request_set)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_DELIVERY_REPORT_REQUEST,
                               g_variant_new_boolean (self->priv->delivery_report_request));

    if (self->priv->teleservice_id != MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_TELESERVICE_ID,
                               g_variant_new_uint32 (self->priv->teleservice_id));

    if (self->priv->service_category != MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_SERVICE_CATEGORY,
                               g_variant_new_uint32 (self->priv->service_category));

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
parse_boolean (const gchar *str,
               GError **error)
{
    if (g_ascii_strcasecmp (str, "yes") == 0 ||
        g_ascii_strcasecmp (str, "true") == 0 ||
        g_str_equal (str, "1"))
        return TRUE;

    if (g_ascii_strcasecmp (str, "no") == 0 ||
        g_ascii_strcasecmp (str, "false") == 0 ||
        g_str_equal (str, "0"))
        return FALSE;

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Invalid properties string, cannot parse '%s' as boolean",
                 str);
    return FALSE;
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

        mm_sms_properties_set_validity_relative (self, n);
    } else if (g_str_equal (key, PROPERTY_CLASS)) {
        gint n = 0;

        if (!mm_get_int_from_str (value, &n)) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_INVALID_ARGS,
                         "Invalid properties string, cannot parse '%s' as int",
                         value);
            return FALSE;
        }

        mm_sms_properties_set_class (self, n);
    } else if (g_str_equal (key, PROPERTY_DELIVERY_REPORT_REQUEST)) {
        GError *inner_error = NULL;
        gboolean request;

        request = parse_boolean (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }

        mm_sms_properties_set_delivery_report_request (self, request);
    } else if (g_str_equal (key, PROPERTY_TELESERVICE_ID)) {
        MMSmsCdmaTeleserviceId teleservice_id;
        GError *inner_error = NULL;

        teleservice_id = mm_common_get_sms_cdma_teleservice_id_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }

        mm_sms_properties_set_teleservice_id (self, teleservice_id);
    } else if (g_str_equal (key, PROPERTY_SERVICE_CATEGORY)) {
        MMSmsCdmaServiceCategory service_category;
        GError *inner_error = NULL;

        service_category = mm_common_get_sms_cdma_service_category_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }

        mm_sms_properties_set_service_category (self, service_category);
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

/**
 * mm_sms_properties_new_from_string: (skip)
 */
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
    else if (g_str_equal (key, PROPERTY_VALIDITY)) {
        guint type;
        GVariant *val;

        g_variant_get (value, "(uv)", &type, &val);
        if (type == MM_SMS_VALIDITY_TYPE_RELATIVE) {
            mm_sms_properties_set_validity_relative (
                properties,
                g_variant_get_uint32 (val));
        } else if (type != MM_SMS_VALIDITY_TYPE_UNKNOWN)
            g_warning ("SMS validity type '%s' not supported yet",
                       mm_sms_validity_type_get_string (type));
        g_variant_unref (val);
    } else if (g_str_equal (key, PROPERTY_CLASS))
        mm_sms_properties_set_class (
            properties,
            g_variant_get_int32 (value));
    else if (g_str_equal (key, PROPERTY_DELIVERY_REPORT_REQUEST))
        mm_sms_properties_set_delivery_report_request (
            properties,
            g_variant_get_boolean (value));
    else if (g_str_equal (key, PROPERTY_TELESERVICE_ID))
        mm_sms_properties_set_teleservice_id (
            properties,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_SERVICE_CATEGORY))
        mm_sms_properties_set_service_category (
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

/**
 * mm_sms_properties_new_from_dictionary: (skip)
 */
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

/**
 * mm_sms_properties_new:
 *
 * Creates a new empty #MMSmsProperties.
 *
 * Returns: (transfer full): a #MMSmsProperties. The returned value should be freed with g_object_unref().
 *
 * Since: 1.0
 */
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
    self->priv->validity_type = MM_SMS_VALIDITY_TYPE_UNKNOWN;
    self->priv->class = -1;
    self->priv->teleservice_id = MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN;
    self->priv->service_category = MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN;
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
