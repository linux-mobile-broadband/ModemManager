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
#include "mm-bearer-properties.h"

/**
 * SECTION: mm-bearer-properties
 * @title: MMBearerProperties
 * @short_description: Helper object to handle bearer properties.
 *
 * The #MMBearerProperties is an object handling the properties requested
 * to ModemManager when creating a new bearer.
 *
 * This object is created by the user and passed to ModemManager with either
 * mm_modem_create_bearer() or mm_modem_create_bearer_sync().
 */

G_DEFINE_TYPE (MMBearerProperties, mm_bearer_properties, G_TYPE_OBJECT);

#define PROPERTY_APN             "apn"
#define PROPERTY_ALLOWED_AUTH    "allowed-auth"
#define PROPERTY_USER            "user"
#define PROPERTY_PASSWORD        "password"
#define PROPERTY_IP_TYPE         "ip-type"
#define PROPERTY_ALLOW_ROAMING   "allow-roaming"
#define PROPERTY_RM_PROTOCOL     "rm-protocol"

/* no longer used properties */
#define DEPRECATED_PROPERTY_NUMBER "number"

struct _MMBearerPropertiesPrivate {
    /* APN */
    gchar *apn;
    /* IP type */
    MMBearerIpFamily ip_type;
    /* Allowed auth */
    MMBearerAllowedAuth allowed_auth;
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

/**
 * mm_bearer_properties_set_apn:
 * @self: a #MMBearerProperties.
 * @apn: Name of the access point.
 *
 * Sets the name of the access point to use when connecting.
 *
 * Since: 1.0
 */
void
mm_bearer_properties_set_apn (MMBearerProperties *self,
                              const gchar *apn)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    g_free (self->priv->apn);
    self->priv->apn = g_strdup (apn);
}

/**
 * mm_bearer_properties_get_apn:
 * @self: a #MMBearerProperties.
 *
 * Gets the name of the access point to use when connecting.
 *
 * Returns: (transfer none): the access point, or #NULL if not set. Do not free
 * the returned value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_bearer_properties_get_apn (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return self->priv->apn;
}

/*****************************************************************************/

/**
 * mm_bearer_properties_set_allowed_auth:
 * @self: a #MMBearerProperties.
 * @allowed_auth: a bitmask of #MMBearerAllowedAuth values.
 *  %MM_BEARER_ALLOWED_AUTH_UNKNOWN may be given to request the modem-default
 *  method.
 *
 * Sets the authentication method to use.
 *
 * Since: 1.0
 */
void
mm_bearer_properties_set_allowed_auth (MMBearerProperties *self,
                                       MMBearerAllowedAuth allowed_auth)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    self->priv->allowed_auth = allowed_auth;
}

/**
 * mm_bearer_properties_get_allowed_auth:
 * @self: a #MMBearerProperties.
 *
 * Gets the authentication methods allowed in the connection.
 *
 * Returns: a bitmask of #MMBearerAllowedAuth values, or
 * %MM_BEARER_ALLOWED_AUTH_UNKNOWN to request the modem-default method.
 *
 * Since: 1.0
 */
MMBearerAllowedAuth
mm_bearer_properties_get_allowed_auth (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), MM_BEARER_ALLOWED_AUTH_UNKNOWN);

    return self->priv->allowed_auth;
}

/*****************************************************************************/

/**
 * mm_bearer_properties_set_user:
 * @self: a #MMBearerProperties.
 * @user: the username
 *
 * Sets the username used to authenticate with the access point.
 *
 * Since: 1.0
 */
void
mm_bearer_properties_set_user (MMBearerProperties *self,
                               const gchar *user)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    g_free (self->priv->user);
    self->priv->user = g_strdup (user);
}

/**
 * mm_bearer_properties_get_user:
 * @self: a #MMBearerProperties.
 *
 * Gets the username used to authenticate with the access point.
 *
 * Returns: (transfer none): the username, or #NULL if not set. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_bearer_properties_get_user (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return self->priv->user;
}

/*****************************************************************************/

/**
 * mm_bearer_properties_set_password:
 * @self: a #MMBearerProperties.
 * @password: the password
 *
 * Sets the password used to authenticate with the access point.
 *
 * Since: 1.0
 */
void
mm_bearer_properties_set_password (MMBearerProperties *self,
                                   const gchar *password)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    g_free (self->priv->password);
    self->priv->password = g_strdup (password);
}

/**
 * mm_bearer_properties_get_password:
 * @self: a #MMBearerProperties.
 *
 * Gets the password used to authenticate with the access point.
 *
 * Returns: (transfer none): the password, or #NULL if not set. Do not free
 * the returned value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_bearer_properties_get_password (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return self->priv->password;
}

/*****************************************************************************/

/**
 * mm_bearer_properties_set_ip_type:
 * @self: a #MMBearerProperties.
 * @ip_type: a #MMBearerIpFamily.
 *
 * Sets the IP type to use.
 *
 * Since: 1.0
 */
void
mm_bearer_properties_set_ip_type (MMBearerProperties *self,
                                  MMBearerIpFamily ip_type)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    self->priv->ip_type = ip_type;
}

/**
 * mm_bearer_properties_get_ip_type:
 * @self: a #MMBearerProperties.
 *
 * Sets the IP type to use.
 *
 * Returns: a #MMBearerIpFamily.
 *
 * Since: 1.0
 */
MMBearerIpFamily
mm_bearer_properties_get_ip_type (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), MM_BEARER_IP_FAMILY_NONE);

    return self->priv->ip_type;
}

/*****************************************************************************/

/**
 * mm_bearer_properties_set_allow_roaming:
 * @self: a #MMBearerProperties.
 * @allow_roaming: boolean value.
 *
 * Sets the flag to indicate whether roaming is allowed or not in the
 * connection.
 *
 * Since: 1.0
 */
void
mm_bearer_properties_set_allow_roaming (MMBearerProperties *self,
                                        gboolean allow_roaming)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    self->priv->allow_roaming = allow_roaming;
    self->priv->allow_roaming_set = TRUE;
}

/**
 * mm_bearer_properties_get_allow_roaming:
 * @self: a #MMBearerProperties.
 *
 * Checks whether roaming is allowed in the connection.
 *
 * Returns: %TRUE if roaming is allowed, %FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_bearer_properties_get_allow_roaming (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), FALSE);

    return self->priv->allow_roaming;
}

/*****************************************************************************/

#ifndef MM_DISABLE_DEPRECATED

/**
 * mm_bearer_properties_set_number:
 * @self: a #MMBearerProperties.
 * @number: the number.
 *
 * Sets the number to use when performing the connection.
 *
 * Since: 1.0
 * Deprecated: 1.10.0. The number setting is not used anywhere, and therefore
 * it doesn't make sense to expose it in the ModemManager interface.
 */
void
mm_bearer_properties_set_number (MMBearerProperties *self,
                                 const gchar *number)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    /* NO-OP */
}

/**
 * mm_bearer_properties_get_number:
 * @self: a #MMBearerProperties.
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
mm_bearer_properties_get_number (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    /* NO-OP */
    return NULL;
}

#endif /* MM_DISABLE_DEPRECATED */

/*****************************************************************************/

/**
 * mm_bearer_properties_set_rm_protocol:
 * @self: a #MMBearerProperties.
 * @protocol: a #MMModemCdmaRmProtocol.
 *
 * Sets the RM protocol to use in the CDMA connection.
 *
 * Since: 1.0
 */
void
mm_bearer_properties_set_rm_protocol (MMBearerProperties *self,
                                      MMModemCdmaRmProtocol protocol)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    self->priv->rm_protocol = protocol;
}

/**
 * mm_bearer_properties_get_rm_protocol:
 * @self: a #MMBearerProperties.
 *
 * Gets the RM protocol requested to use in the CDMA connection.
 *
 * Returns: a #MMModemCdmaRmProtocol.
 *
 * Since: 1.0
 */
MMModemCdmaRmProtocol
mm_bearer_properties_get_rm_protocol (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN);

    return self->priv->rm_protocol;
}

/*****************************************************************************/

/**
 * mm_bearer_properties_get_dictionary: (skip)
 */
GVariant *
mm_bearer_properties_get_dictionary (MMBearerProperties *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->apn)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_APN,
                               g_variant_new_string (self->priv->apn));

    if (self->priv->allowed_auth != MM_BEARER_ALLOWED_AUTH_UNKNOWN)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ALLOWED_AUTH,
                               g_variant_new_uint32 (self->priv->allowed_auth));

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

    if (self->priv->ip_type != MM_BEARER_IP_FAMILY_NONE)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_IP_TYPE,
                               g_variant_new_uint32 (self->priv->ip_type));

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

/**
 * mm_bearer_properties_consume_string: (skip)
 */
gboolean
mm_bearer_properties_consume_string (MMBearerProperties *self,
                                     const gchar *key,
                                     const gchar *value,
                                     GError **error)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), FALSE);

    if (g_str_equal (key, PROPERTY_APN))
        mm_bearer_properties_set_apn (self, value);
    else if (g_str_equal (key, PROPERTY_ALLOWED_AUTH)) {
        GError *inner_error = NULL;
        MMBearerAllowedAuth allowed_auth;

        allowed_auth = mm_common_get_allowed_auth_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_bearer_properties_set_allowed_auth (self, allowed_auth);
    } else if (g_str_equal (key, PROPERTY_USER))
        mm_bearer_properties_set_user (self, value);
    else if (g_str_equal (key, PROPERTY_PASSWORD))
        mm_bearer_properties_set_password (self, value);
    else if (g_str_equal (key, PROPERTY_IP_TYPE)) {
        GError *inner_error = NULL;
        MMBearerIpFamily ip_type;

        ip_type = mm_common_get_ip_type_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_bearer_properties_set_ip_type (self, ip_type);
    } else if (g_str_equal (key, PROPERTY_ALLOW_ROAMING)) {
        GError *inner_error = NULL;
        gboolean allow_roaming;

        allow_roaming = mm_common_get_boolean_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_bearer_properties_set_allow_roaming (self, allow_roaming);
    } else if (g_str_equal (key, PROPERTY_RM_PROTOCOL)) {
        GError *inner_error = NULL;
        MMModemCdmaRmProtocol protocol;

        protocol = mm_common_get_rm_protocol_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_bearer_properties_set_rm_protocol (self, protocol);
    } else if (g_str_equal (key, DEPRECATED_PROPERTY_NUMBER)) {
        /* NO-OP */
    } else {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "Invalid properties string, unsupported key '%s'",
                     key);
        return FALSE;
    }

    return TRUE;
}

typedef struct {
    MMBearerProperties *properties;
    GError *error;
} ParseKeyValueContext;

static gboolean
key_value_foreach (const gchar *key,
                   const gchar *value,
                   ParseKeyValueContext *ctx)
{
    return mm_bearer_properties_consume_string (ctx->properties,
                                                key,
                                                value,
                                                &ctx->error);
}

/**
 * mm_bearer_properties_new_from_string: (skip)
 */
MMBearerProperties *
mm_bearer_properties_new_from_string (const gchar *str,
                                      GError **error)
{
    ParseKeyValueContext ctx;

    ctx.error = NULL;
    ctx.properties = mm_bearer_properties_new ();

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

/**
 * mm_bearer_properties_consume_variant: (skip)
 */
gboolean
mm_bearer_properties_consume_variant (MMBearerProperties *properties,
                                      const gchar *key,
                                      GVariant *value,
                                      GError **error)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (properties), FALSE);

    if (g_str_equal (key, PROPERTY_APN))
        mm_bearer_properties_set_apn (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_ALLOWED_AUTH))
        mm_bearer_properties_set_allowed_auth (
            properties,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_USER))
        mm_bearer_properties_set_user (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_PASSWORD))
        mm_bearer_properties_set_password (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_IP_TYPE))
        mm_bearer_properties_set_ip_type (
            properties,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_ALLOW_ROAMING))
        mm_bearer_properties_set_allow_roaming (
            properties,
            g_variant_get_boolean (value));
    else if (g_str_equal (key, PROPERTY_RM_PROTOCOL))
        mm_bearer_properties_set_rm_protocol (
            properties,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, DEPRECATED_PROPERTY_NUMBER)) {
        /* NO-OP */
    } else {
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
 * mm_bearer_properties_new_from_dictionary: (skip)
 */
MMBearerProperties *
mm_bearer_properties_new_from_dictionary (GVariant *dictionary,
                                          GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMBearerProperties *properties;

    properties = mm_bearer_properties_new ();
    if (!dictionary)
        return properties;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create Bearer properties from dictionary: "
                     "invalid variant type received");
        g_object_unref (properties);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        mm_bearer_properties_consume_variant (properties,
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

static gboolean
cmp_str (const gchar                *a,
         const gchar                *b,
         MMBearerPropertiesCmpFlags  flags)
{
    /* Strict match */
    if ((!a && !b) || (a && b && g_strcmp0 (a, b) == 0))
        return TRUE;
    /* Additional loose match, consider NULL and EMPTY string equal */
    if (flags & MM_BEARER_PROPERTIES_CMP_FLAGS_LOOSE) {
        if ((!a && !b[0]) || (!b && !a[0]))
            return TRUE;
    }
    return FALSE;
}

static gboolean
cmp_ip_type (MMBearerIpFamily           a,
             MMBearerIpFamily           b,
             MMBearerPropertiesCmpFlags flags)
{
    /* Strict match */
    if (a == b)
        return TRUE;
    /* Additional loose match NONE == IPV4 */
    if (flags & MM_BEARER_PROPERTIES_CMP_FLAGS_LOOSE) {
        if ((a == MM_BEARER_IP_FAMILY_NONE && b == MM_BEARER_IP_FAMILY_IPV4) ||
            (b == MM_BEARER_IP_FAMILY_NONE && a == MM_BEARER_IP_FAMILY_IPV4))
            return TRUE;
    }
    return FALSE;
}

static gboolean
cmp_allowed_auth (MMBearerAllowedAuth        a,
                  MMBearerAllowedAuth        b,
                  MMBearerPropertiesCmpFlags flags)
{
    /* Strict match */
    if (a == b)
        return TRUE;
    /* Additional loose match UNKNOWN == NONE */
    if (flags & MM_BEARER_PROPERTIES_CMP_FLAGS_LOOSE) {
        if ((a == MM_BEARER_ALLOWED_AUTH_UNKNOWN && b == MM_BEARER_ALLOWED_AUTH_NONE) ||
            (b == MM_BEARER_ALLOWED_AUTH_UNKNOWN && a == MM_BEARER_ALLOWED_AUTH_NONE))
            return TRUE;
    }
    return FALSE;
}

/**
 * mm_bearer_properties_cmp: (skip)
 */
gboolean
mm_bearer_properties_cmp (MMBearerProperties         *a,
                          MMBearerProperties         *b,
                          MMBearerPropertiesCmpFlags  flags)
{
    if (!cmp_str (a->priv->apn, b->priv->apn, flags))
        return FALSE;
    if (!cmp_ip_type (a->priv->ip_type, b->priv->ip_type, flags))
        return FALSE;
    if (!cmp_allowed_auth (a->priv->allowed_auth, b->priv->allowed_auth, flags))
        return FALSE;
    if (!cmp_str (a->priv->user, b->priv->user, flags))
        return FALSE;
    if (!(flags & MM_BEARER_PROPERTIES_CMP_FLAGS_NO_PASSWORD) && !cmp_str (a->priv->password, b->priv->password, flags))
        return FALSE;
    if (!(flags & MM_BEARER_PROPERTIES_CMP_FLAGS_NO_ALLOW_ROAMING)) {
        if (a->priv->allow_roaming != b->priv->allow_roaming)
            return FALSE;
        if (a->priv->allow_roaming_set != b->priv->allow_roaming_set)
            return FALSE;
    }
    if (!(flags & MM_BEARER_PROPERTIES_CMP_FLAGS_NO_RM_PROTOCOL)) {
        if (a->priv->rm_protocol != b->priv->rm_protocol)
            return FALSE;
    }
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_bearer_properties_new:
 *
 * Creates a new empty #MMBearerProperties.
 *
 * Returns: (transfer full): a #MMBearerProperties. The returned value should be freed with g_object_unref().
 *
 * Since: 1.0
 */
MMBearerProperties *
mm_bearer_properties_new (void)
{
    return (MM_BEARER_PROPERTIES (
                g_object_new (MM_TYPE_BEARER_PROPERTIES, NULL)));
}

static void
mm_bearer_properties_init (MMBearerProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BEARER_PROPERTIES,
                                              MMBearerPropertiesPrivate);

    /* Some defaults */
    self->priv->allow_roaming = TRUE;
    self->priv->rm_protocol = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
    self->priv->allowed_auth = MM_BEARER_ALLOWED_AUTH_UNKNOWN;
    self->priv->ip_type = MM_BEARER_IP_FAMILY_NONE;
}

static void
finalize (GObject *object)
{
    MMBearerProperties *self = MM_BEARER_PROPERTIES (object);

    g_free (self->priv->apn);
    g_free (self->priv->user);
    g_free (self->priv->password);

    G_OBJECT_CLASS (mm_bearer_properties_parent_class)->finalize (object);
}

static void
mm_bearer_properties_class_init (MMBearerPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerPropertiesPrivate));

    object_class->finalize = finalize;
}
