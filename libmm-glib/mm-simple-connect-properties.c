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
#include "mm-simple-connect-properties.h"

/**
 * SECTION: mm-simple-connect-properties
 * @title: MMSimpleConnectProperties
 * @short_description: Helper object to handle connection properties.
 *
 * The #MMSimpleConnectProperties is an object handling the properties requested
 * to ModemManager when launching a connection with the Simple interface.
 *
 * This object is created by the user and passed to ModemManager with either
 * mm_modem_simple_connect() or mm_modem_simple_connect_sync().
 */

G_DEFINE_TYPE (MMSimpleConnectProperties, mm_simple_connect_properties, G_TYPE_OBJECT)

#define PROPERTY_PIN         "pin"
#define PROPERTY_OPERATOR_ID "operator-id"

struct _MMSimpleConnectPropertiesPrivate {
    /* PIN */
    gchar *pin;
    /* Operator ID */
    gchar *operator_id;
    /* Bearer properties */
    MMBearerProperties *bearer_properties;
};

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_pin:
 * @self: a #MMSimpleConnectProperties.
 * @pin: PIN code.
 *
 * Sets the PIN code to use when unlocking the modem.
 *
 * Since: 1.0
 */
void
mm_simple_connect_properties_set_pin (MMSimpleConnectProperties *self,
                                      const gchar *pin)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    g_free (self->priv->pin);
    self->priv->pin = g_strdup (pin);
}

/**
 * mm_simple_connect_properties_get_pin:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the PIN code to use when unlocking the modem.
 *
 * Returns: (transfer none): the PIN, or #NULL if not set. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_simple_connect_properties_get_pin (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return self->priv->pin;
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_operator_id:
 * @self: a #MMSimpleConnectProperties.
 * @operator_id: operator ID, given as MCC/MNC.
 *
 * Sets the ID of the network to which register before connecting.
 *
 * Since: 1.0
 */
void
mm_simple_connect_properties_set_operator_id (MMSimpleConnectProperties *self,
                                              const gchar *operator_id)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    g_free (self->priv->operator_id);
    self->priv->operator_id = g_strdup (operator_id);
}

/**
 * mm_simple_connect_properties_get_operator_id:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the ID of the network to which register before connecting.
 *
 * Returns: (transfer none): the operator ID, or #NULL if not set. Do not free
 * the returned value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_simple_connect_properties_get_operator_id (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return self->priv->operator_id;
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_apn:
 * @self: a #MMSimpleConnectProperties.
 * @apn: Name of the access point.
 *
 * Sets the name of the access point to use when connecting.
 *
 * Since: 1.0
 */
void
mm_simple_connect_properties_set_apn (MMSimpleConnectProperties *self,
                                      const gchar *apn)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_apn (self->priv->bearer_properties,
                                  apn);
}

/**
 * mm_simple_connect_properties_get_apn:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the name of the access point to use when connecting.
 *
 * Returns: (transfer none): the access point, or #NULL if not set. Do not free
 * the returned value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_simple_connect_properties_get_apn (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return mm_bearer_properties_get_apn (self->priv->bearer_properties);
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_allowed_auth:
 * @self: a #MMSimpleConnectProperties.
 * @allowed_auth: a bitmask of #MMBearerAllowedAuth values.
 *  %MM_BEARER_ALLOWED_AUTH_UNKNOWN may be given to request the modem-default method.
 *
 * Sets the authentication method to use.
 *
 * Since: 1.0
 */
void
mm_simple_connect_properties_set_allowed_auth (MMSimpleConnectProperties *self,
                                               MMBearerAllowedAuth allowed_auth)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_allowed_auth (self->priv->bearer_properties, allowed_auth);
}

/**
 * mm_simple_connect_properties_get_allowed_auth:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the authentication methods allowed in the connection.
 *
 * Returns: a bitmask of #MMBearerAllowedAuth values, or
 * %MM_BEARER_ALLOWED_AUTH_UNKNOWN to request the modem-default method.
 *
 * Since: 1.0
 */
MMBearerAllowedAuth
mm_simple_connect_properties_get_allowed_auth (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), MM_BEARER_ALLOWED_AUTH_UNKNOWN);

    return mm_bearer_properties_get_allowed_auth (self->priv->bearer_properties);
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_user:
 * @self: a #MMSimpleConnectProperties.
 * @user: the username
 *
 * Sets the username used to authenticate with the access point.
 *
 * Since: 1.0
 */
void
mm_simple_connect_properties_set_user (MMSimpleConnectProperties *self,
                                       const gchar *user)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_user (self->priv->bearer_properties,
                                   user);
}

/**
 * mm_simple_connect_properties_get_user:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the username used to authenticate with the access point.
 *
 * Returns: (transfer none): the username, or #NULL if not set. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_simple_connect_properties_get_user (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return mm_bearer_properties_get_user (self->priv->bearer_properties);
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_password:
 * @self: a #MMSimpleConnectProperties.
 * @password: the password
 *
 * Sets the password used to authenticate with the access point.
 *
 * Since: 1.0
 */
void
mm_simple_connect_properties_set_password (MMSimpleConnectProperties *self,
                                           const gchar *password)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_password (self->priv->bearer_properties,
                                       password);
}

/**
 * mm_simple_connect_properties_get_password:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the password used to authenticate with the access point.
 *
 * Returns: (transfer none): the password, or #NULL if not set. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_simple_connect_properties_get_password (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return mm_bearer_properties_get_password (self->priv->bearer_properties);
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_ip_type:
 * @self: a #MMSimpleConnectProperties.
 * @ip_type: a #MMBearerIpFamily.
 *
 * Sets the IP type to use.
 *
 * Since: 1.0
 */
void
mm_simple_connect_properties_set_ip_type (MMSimpleConnectProperties *self,
                                          MMBearerIpFamily ip_type)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_ip_type (self->priv->bearer_properties,
                                      ip_type);
}

/**
 * mm_simple_connect_properties_get_ip_type:
 * @self: a #MMSimpleConnectProperties.
 *
 * Sets the IP type to use.
 *
 * Returns: a #MMBearerIpFamily.
 *
 * Since: 1.0
 */
MMBearerIpFamily
mm_simple_connect_properties_get_ip_type (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), MM_BEARER_IP_FAMILY_NONE);

    return mm_bearer_properties_get_ip_type (self->priv->bearer_properties);
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_allow_roaming:
 * @self: a #MMSimpleConnectProperties.
 * @allow_roaming: boolean value.
 *
 * Sets the flag to indicate whether roaming is allowed or not in the
 * connection.
 *
 * Since: 1.0
 */
void
mm_simple_connect_properties_set_allow_roaming (MMSimpleConnectProperties *self,
                                                gboolean allow_roaming)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_allow_roaming (self->priv->bearer_properties,
                                            allow_roaming);
}

/**
 * mm_simple_connect_properties_get_allow_roaming:
 * @self: a #MMSimpleConnectProperties.
 *
 * Checks whether roaming is allowed in the connection.
 *
 * Returns: %TRUE if roaming is allowed, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_simple_connect_properties_get_allow_roaming (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), FALSE);

    return mm_bearer_properties_get_allow_roaming (self->priv->bearer_properties);
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_rm_protocol:
 * @self: a #MMSimpleConnectProperties.
 * @protocol: a #MMModemCdmaRmProtocol.
 *
 * Sets the RM protocol requested by the user.
 *
 * Since: 1.16
 */
void
mm_simple_connect_properties_set_rm_protocol (MMSimpleConnectProperties *self,
                                              MMModemCdmaRmProtocol      protocol)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_rm_protocol (self->priv->bearer_properties, protocol);
}

/**
 * mm_simple_connect_properties_get_rm_protocol:
 * @self: a #MMSimpleConnectProperties.
 *
 * Get the RM protocol requested by the user.
 *
 * Returns: a #MMModemCdmaRmProtocol.
 *
 * Since: 1.16
 */
MMModemCdmaRmProtocol
mm_simple_connect_properties_get_rm_protocol (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN);

    return mm_bearer_properties_get_rm_protocol (self->priv->bearer_properties);
}

/*****************************************************************************/

#ifndef MM_DISABLE_DEPRECATED

/**
 * mm_simple_connect_properties_set_number:
 * @self: a #MMSimpleConnectProperties.
 * @number: the number.
 *
 * Sets the number to use when performing the connection.
 *
 * Since: 1.0
 * Deprecated: 1.10.0. The number setting is not used anywhere, and therefore
 * it doesn't make sense to expose it in the ModemManager interface.
 */
void
mm_simple_connect_properties_set_number (MMSimpleConnectProperties *self,
                                         const gchar *number)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    /* NO-OP */
}

/**
 * mm_simple_connect_properties_get_number:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the number to use when performing the connection.
 *
 * Returns: (transfer none): the number, or #NULL if not set. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.0
 * Deprecated: 1.10.0. The number setting is not used anywhere, and therefore
 * it doesn't make sense to expose it in the ModemManager interface.
 */
const gchar *
mm_simple_connect_properties_get_number (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    /* NO-OP */
    return NULL;
}

#endif /* MM_DISABLE_DEPRECATED */

/*****************************************************************************/

/**
 * mm_simple_connect_properties_get_bearer_properties: (skip)
 */
MMBearerProperties *
mm_simple_connect_properties_get_bearer_properties (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return g_object_ref (self->priv->bearer_properties);
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_get_dictionary: (skip)
 */
GVariant *
mm_simple_connect_properties_get_dictionary (MMSimpleConnectProperties *self)
{
    GVariantBuilder builder;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    GVariant *bearer_properties_dictionary;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

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

    /* Merge dictionaries */
    bearer_properties_dictionary = mm_bearer_properties_get_dictionary (self->priv->bearer_properties);
    g_variant_iter_init (&iter, bearer_properties_dictionary);
    while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        g_variant_builder_add (&builder,
                               "{sv}",
                               key,
                               value);
        g_variant_unref (value);
        g_free (key);
    }
    g_variant_unref (bearer_properties_dictionary);

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

typedef struct {
    MMSimpleConnectProperties *self;
    GError *error;
    gchar *allowed_modes_str;
    gchar *preferred_mode_str;
} ParseKeyValueContext;

static gboolean
key_value_foreach (const gchar *key,
                   const gchar *value,
                   ParseKeyValueContext *ctx)
{
    GError *inner_error = NULL;

    /* First, check if we can consume this as bearer properties */
    if (mm_bearer_properties_consume_string (ctx->self->priv->bearer_properties,
                                             key, value,
                                             &inner_error))
        return TRUE;

    /* Unknown keys are reported as unsupported. Any other error is right away
     * fatal (e.g. an invalid value given to a known bearer property) */
    if (!g_error_matches (inner_error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED)) {
        ctx->error = inner_error;
        return FALSE;
    }

    /* On unsupported errors, try with the Simple.Connect specific properties */
    g_clear_error (&inner_error);

    if (g_str_equal (key, PROPERTY_PIN))
        mm_simple_connect_properties_set_pin (ctx->self, value);
    else if (g_str_equal (key, PROPERTY_OPERATOR_ID))
        mm_simple_connect_properties_set_operator_id (ctx->self, value);
    else {
        ctx->error = g_error_new (MM_CORE_ERROR,
                                  MM_CORE_ERROR_UNSUPPORTED,
                                  "Invalid properties string, unsupported key '%s'",
                                  key);
    }

    return !ctx->error;
}

/**
 * mm_simple_connect_properties_new_from_string: (skip)
 */
MMSimpleConnectProperties *
mm_simple_connect_properties_new_from_string (const gchar *str,
                                              GError **error)
{
    ParseKeyValueContext ctx;

    ctx.error = NULL;
    ctx.self = mm_simple_connect_properties_new ();

    mm_common_parse_key_value_string (str,
                                      &ctx.error,
                                      (MMParseKeyValueForeachFn)key_value_foreach,
                                      &ctx);

    /* If error, destroy the object */
    if (ctx.error) {
        g_propagate_error (error, ctx.error);
        g_object_unref (ctx.self);
        ctx.self = NULL;
    }

    return ctx.self;
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_new_from_dictionary: (skip)
 */
MMSimpleConnectProperties *
mm_simple_connect_properties_new_from_dictionary (GVariant *dictionary,
                                                  GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMSimpleConnectProperties *self;

    self = mm_simple_connect_properties_new ();
    if (!dictionary)
        return self;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create Simple Connect properties from dictionary: "
                     "invalid variant type received");
        g_object_unref (self);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {

        /* First, check if we can consume this as bearer properties */
        if (!mm_bearer_properties_consume_variant (self->priv->bearer_properties,
                                                   key, value,
                                                   NULL)) {
            if (g_str_equal (key, PROPERTY_PIN))
                mm_simple_connect_properties_set_pin (
                    self,
                    g_variant_get_string (value, NULL));
            else if (g_str_equal (key, PROPERTY_OPERATOR_ID))
                mm_simple_connect_properties_set_operator_id (
                    self,
                    g_variant_get_string (value, NULL));
            else {
                /* Set inner error, will stop the loop */
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_INVALID_ARGS,
                                           "Invalid properties dictionary, unexpected key '%s'",
                                           key);
            }
        }

        g_free (key);
        g_variant_unref (value);
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (self);
        self = NULL;
    }

    return self;
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_new:
 *
 * Creates a new empty #MMSimpleConnectProperties.
 *
 * Returns: (transfer full): a #MMSimpleConnectProperties. The returned value should be freed with g_object_unref().
 *
 * Since: 1.0
 */
MMSimpleConnectProperties *
mm_simple_connect_properties_new (void)
{
    return (MM_SIMPLE_CONNECT_PROPERTIES (
                g_object_new (MM_TYPE_SIMPLE_CONNECT_PROPERTIES, NULL)));
}

static void
mm_simple_connect_properties_init (MMSimpleConnectProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_SIMPLE_CONNECT_PROPERTIES,
                                              MMSimpleConnectPropertiesPrivate);

    /* Some defaults */
    self->priv->bearer_properties = mm_bearer_properties_new ();
}

static void
finalize (GObject *object)
{
    MMSimpleConnectProperties *self = MM_SIMPLE_CONNECT_PROPERTIES (object);

    g_free (self->priv->pin);
    g_free (self->priv->operator_id);
    g_object_unref (self->priv->bearer_properties);

    G_OBJECT_CLASS (mm_simple_connect_properties_parent_class)->finalize (object);
}

static void
mm_simple_connect_properties_class_init (MMSimpleConnectPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSimpleConnectPropertiesPrivate));

    object_class->finalize = finalize;
}
