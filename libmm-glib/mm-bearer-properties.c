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
 * Copyright (C) 2011-2022 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2022 Google, Inc.
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-enums-types.h"
#include "mm-flags-types.h"
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

G_DEFINE_TYPE (MMBearerProperties, mm_bearer_properties, G_TYPE_OBJECT)

#define PROPERTY_ALLOW_ROAMING "allow-roaming"
#define PROPERTY_RM_PROTOCOL   "rm-protocol"
#define PROPERTY_MULTIPLEX     "multiplex"

/* no longer used properties */
#define DEPRECATED_PROPERTY_NUMBER "number"

struct _MMBearerPropertiesPrivate {
    /* The 3GPP profile is a subset of the bearer properties */
    MM3gppProfile *profile;

    /* Roaming allowance */
    gboolean allow_roaming_set;
    gboolean allow_roaming;
    /* Protocol of the Rm interface */
    MMModemCdmaRmProtocol rm_protocol;
    /* Multiplex support */
    MMBearerMultiplexSupport multiplex;
};

/*****************************************************************************/

/**
 * mm_bearer_properties_set_profile_name:
 * @self: a #MMBearerProperties.
 * @profile_name: Name of the profile.
 *
 * Sets the name of the profile to use when connecting.
 *
 * Since: 1.20
 */
void
mm_bearer_properties_set_profile_name (MMBearerProperties *self,
                                       const gchar        *profile_name)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_3gpp_profile_set_profile_name (self->priv->profile, profile_name);
}

/**
 * mm_bearer_properties_get_profile_name:
 * @self: a #MMBearerProperties.
 *
 * Gets the name of the profile to use when connecting.
 *
 * Returns: (transfer none): the profile name, or #NULL if not set. Do not free
 * the returned value, it is owned by @self.
 *
 * Since: 1.20
 */
const gchar *
mm_bearer_properties_get_profile_name (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return mm_3gpp_profile_get_profile_name (self->priv->profile);
}
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
                              const gchar        *apn)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_3gpp_profile_set_apn (self->priv->profile, apn);
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

    return mm_3gpp_profile_get_apn (self->priv->profile);
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
mm_bearer_properties_set_allowed_auth (MMBearerProperties  *self,
                                       MMBearerAllowedAuth  allowed_auth)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_3gpp_profile_set_allowed_auth (self->priv->profile, allowed_auth);
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

    return mm_3gpp_profile_get_allowed_auth (self->priv->profile);
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
                               const gchar        *user)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_3gpp_profile_set_user (self->priv->profile, user);
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

    return mm_3gpp_profile_get_user (self->priv->profile);
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
                                   const gchar        *password)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_3gpp_profile_set_password (self->priv->profile, password);
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

    return mm_3gpp_profile_get_password (self->priv->profile);
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
                                  MMBearerIpFamily    ip_type)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_3gpp_profile_set_ip_type (self->priv->profile, ip_type);
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

    return mm_3gpp_profile_get_ip_type (self->priv->profile);
}

/*****************************************************************************/

/**
 * mm_bearer_properties_set_apn_type:
 * @self: a #MMBearerProperties.
 * @apn_type: a mask of #MMBearerApnType values.
 *
 * Sets the APN types to use.
 *
 * Since: 1.18
 */
void
mm_bearer_properties_set_apn_type (MMBearerProperties *self,
                                   MMBearerApnType     apn_type)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_3gpp_profile_set_apn_type (self->priv->profile, apn_type);
}

/**
 * mm_bearer_properties_get_apn_type:
 * @self: a #MMBearerProperties.
 *
 * Gets the APN types to use.
 *
 * Returns: a mask of #MMBearerApnType values.
 *
 * Since: 1.18
 */
MMBearerApnType
mm_bearer_properties_get_apn_type (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), MM_BEARER_APN_TYPE_NONE);

    return mm_3gpp_profile_get_apn_type (self->priv->profile);
}

/*****************************************************************************/

/**
 * mm_bearer_properties_set_profile_id:
 * @self: a #MMBearerProperties.
 * @profile_id: a profile id.
 *
 * Sets the profile ID to use.
 *
 * Since: 1.18
 */
void
mm_bearer_properties_set_profile_id (MMBearerProperties *self,
                                     gint                profile_id)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_3gpp_profile_set_profile_id (self->priv->profile, profile_id);
}

/**
 * mm_bearer_properties_get_profile_id:
 * @self: a #MMBearerProperties.
 *
 * Gets the profile ID to use.
 *
 * Returns: the profile id.
 *
 * Since: 1.18
 */
gint
mm_bearer_properties_get_profile_id (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), MM_3GPP_PROFILE_ID_UNKNOWN);

    return mm_3gpp_profile_get_profile_id (self->priv->profile);
}

/*****************************************************************************/

/**
 * mm_bearer_properties_set_access_type_preference:
 * @self: a #MMBearerProperties.
 * @access_type_preference: a #MMBearerAccessTypePreference value.
 *
 * Sets the 5G network access type preference.
 *
 * Since: 1.20
 */
void
mm_bearer_properties_set_access_type_preference (MMBearerProperties           *self,
                                                 MMBearerAccessTypePreference  access_type_preference)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_3gpp_profile_set_access_type_preference (self->priv->profile, access_type_preference);
}

/**
 * mm_bearer_properties_get_access_type_preference:
 * @self: a #MMBearerProperties.
 *
 * Gets the 5G network access type preference.
 *
 * Returns: a #MMBearerAccessTypePreference value.
 *
 * Since: 1.20
 */
MMBearerAccessTypePreference
mm_bearer_properties_get_access_type_preference (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), MM_BEARER_ACCESS_TYPE_PREFERENCE_NONE);

    return mm_3gpp_profile_get_access_type_preference (self->priv->profile);
}

/*****************************************************************************/

/**
 * mm_bearer_properties_set_roaming_allowance:
 * @self: a #MMBearerProperties.
 * @roaming_allowance: a mask of #MMBearerRoamingAllowance values
 *
 * Sets the roaming allowance rules.
 *
 * Since: 1.20
 */
void
mm_bearer_properties_set_roaming_allowance (MMBearerProperties       *self,
                                            MMBearerRoamingAllowance  roaming_allowance)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_3gpp_profile_set_roaming_allowance (self->priv->profile, roaming_allowance);
}

/**
 * mm_bearer_properties_get_roaming_allowance:
 * @self: a #MMBearerProperties.
 *
 * Gets the roaming allowance rules.
 *
 * Returns: a mask of #MMBearerRoamingAllowance values.
 *
 * Since: 1.20
 */
MMBearerRoamingAllowance
mm_bearer_properties_get_roaming_allowance (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), MM_BEARER_ROAMING_ALLOWANCE_NONE);

    return mm_3gpp_profile_get_roaming_allowance (self->priv->profile);
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
 * mm_bearer_properties_set_multiplex:
 * @self: a #MMBearerProperties.
 * @multiplex: a #MMBearerMultiplexSupport.
 *
 * Gets the type of multiplex support requested by the user.
 *
 * Since: 1.18
 */
void
mm_bearer_properties_set_multiplex (MMBearerProperties       *self,
                                    MMBearerMultiplexSupport  multiplex)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    self->priv->multiplex = multiplex;
}

/**
 * mm_bearer_properties_get_multiplex:
 * @self: a #MMBearerProperties.
 *
 * Gets the type of multiplex support requested by the user.
 *
 * Returns: a #MMBearerMultiplexSupport.
 *
 * Since: 1.18
 */
MMBearerMultiplexSupport
mm_bearer_properties_get_multiplex (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), MM_BEARER_MULTIPLEX_SUPPORT_UNKNOWN);

    return self->priv->multiplex;
}

/*****************************************************************************/

/**
 * mm_bearer_properties_peek_3gpp_profile: (skip)
 */
MM3gppProfile *
mm_bearer_properties_peek_3gpp_profile (MMBearerProperties *self)
{
    return self->priv->profile;
}

/*****************************************************************************/

/**
 * mm_bearer_properties_get_dictionary: (skip)
 */
GVariant *
mm_bearer_properties_get_dictionary (MMBearerProperties *self)
{
    GVariantBuilder  builder;
    GVariantIter     iter;
    gchar           *key;
    GVariant        *value;
    GVariant        *profile_dictionary;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

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

    if (self->priv->multiplex)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_MULTIPLEX,
                               g_variant_new_uint32 (self->priv->multiplex));

    /* Merge dictionaries */
    profile_dictionary = mm_3gpp_profile_get_dictionary (self->priv->profile);
    g_variant_iter_init (&iter, profile_dictionary);
    while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        g_variant_builder_add (&builder, "{sv}", key, value);
        g_variant_unref (value);
        g_free (key);
    }
    g_variant_unref (profile_dictionary);

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

/**
 * mm_bearer_properties_consume_string: (skip)
 */
gboolean
mm_bearer_properties_consume_string (MMBearerProperties  *self,
                                     const gchar         *key,
                                     const gchar         *value,
                                     GError             **error)
{
    GError *inner_error = NULL;

    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), FALSE);

    /* First, check if we can consume this as bearer properties */
    if (mm_3gpp_profile_consume_string (self->priv->profile, key, value, &inner_error))
        return TRUE;

    /* Unknown keys are reported as unsupported. Any other error is right away
     * fatal (e.g. an invalid value given to a known profile property) */
    if (!g_error_matches (inner_error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED)) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    /* On unsupported errors, try with the bearer specific properties */
    g_clear_error (&inner_error);

    if (g_str_equal (key, PROPERTY_ALLOW_ROAMING)) {
        gboolean allow_roaming;

        allow_roaming = mm_common_get_boolean_from_string (value, &inner_error);
        if (!inner_error)
            mm_bearer_properties_set_allow_roaming (self, allow_roaming);
    } else if (g_str_equal (key, PROPERTY_RM_PROTOCOL)) {
        MMModemCdmaRmProtocol protocol;

        protocol = mm_common_get_rm_protocol_from_string (value, &inner_error);
        if (!inner_error)
            mm_bearer_properties_set_rm_protocol (self, protocol);
    } else if (g_str_equal (key, PROPERTY_MULTIPLEX)) {
        MMBearerMultiplexSupport multiplex;

        multiplex = mm_common_get_multiplex_support_from_string (value, &inner_error);
        if (!inner_error)
            mm_bearer_properties_set_multiplex (self, multiplex);
    } else if (g_str_equal (key, DEPRECATED_PROPERTY_NUMBER)) {
        /* NO-OP */
    } else {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                   "Invalid properties string, unsupported key '%s'", key);
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    return TRUE;
}

typedef struct {
    MMBearerProperties *properties;
    GError             *error;
} ParseKeyValueContext;

static gboolean
key_value_foreach (const gchar          *key,
                   const gchar          *value,
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
mm_bearer_properties_consume_variant (MMBearerProperties  *self,
                                      const gchar         *key,
                                      GVariant            *value,
                                      GError             **error)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), FALSE);

    /* First, check if we can consume this as profile properties */
    if (mm_3gpp_profile_consume_variant (self->priv->profile, key, value, NULL))
        return TRUE;

    if (g_str_equal (key, PROPERTY_ALLOW_ROAMING))
        mm_bearer_properties_set_allow_roaming (self, g_variant_get_boolean (value));
    else if (g_str_equal (key, PROPERTY_RM_PROTOCOL))
        mm_bearer_properties_set_rm_protocol (self, g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_MULTIPLEX))
        mm_bearer_properties_set_multiplex (self, g_variant_get_uint32 (value));
    else if (g_str_equal (key, DEPRECATED_PROPERTY_NUMBER)) {
        /* NO-OP */
    } else {
        /* Set error */
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid properties dictionary, unexpected key '%s'", key);
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
        mm_bearer_properties_consume_variant (properties, key, value, &inner_error);
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
cmp_apn_type (MMBearerApnType            a,
              MMBearerApnType            b,
              MMBearerPropertiesCmpFlags flags)
{
    /* Strict match */
    if (a == b)
        return TRUE;
    /* Additional loose match NONE == DEFAULT */
    if (flags & MM_BEARER_PROPERTIES_CMP_FLAGS_LOOSE) {
        if ((a == MM_BEARER_APN_TYPE_NONE && b == MM_BEARER_APN_TYPE_DEFAULT) ||
            (b == MM_BEARER_APN_TYPE_NONE && a == MM_BEARER_APN_TYPE_DEFAULT))
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
    /* MBIM and QMI fallback to CHAP when a username or password is present,
       but no authentication type was provided */
    if (flags & MM_BEARER_PROPERTIES_CMP_FLAGS_LOOSE) {
        if ((a == MM_BEARER_ALLOWED_AUTH_UNKNOWN && b == MM_BEARER_ALLOWED_AUTH_NONE) ||
            (b == MM_BEARER_ALLOWED_AUTH_UNKNOWN && a == MM_BEARER_ALLOWED_AUTH_NONE) ||
            (a == MM_BEARER_ALLOWED_AUTH_UNKNOWN && b == MM_BEARER_ALLOWED_AUTH_CHAP) ||
            (b == MM_BEARER_ALLOWED_AUTH_UNKNOWN && a == MM_BEARER_ALLOWED_AUTH_CHAP) )
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
    /* we don't have any other need to compare profiles, so just compare the properties here */
    if (!cmp_str (mm_3gpp_profile_get_apn (a->priv->profile), mm_3gpp_profile_get_apn (b->priv->profile), flags))
        return FALSE;
    if (!cmp_ip_type (mm_3gpp_profile_get_ip_type (a->priv->profile), mm_3gpp_profile_get_ip_type (b->priv->profile), flags))
        return FALSE;
    if (!cmp_allowed_auth (mm_3gpp_profile_get_allowed_auth (a->priv->profile), mm_3gpp_profile_get_allowed_auth (b->priv->profile), flags))
        return FALSE;
    if (!cmp_str (mm_3gpp_profile_get_user (a->priv->profile), mm_3gpp_profile_get_user (b->priv->profile), flags))
        return FALSE;
    if (!(flags & MM_BEARER_PROPERTIES_CMP_FLAGS_NO_PASSWORD) &&
        !cmp_str (mm_3gpp_profile_get_password (a->priv->profile), mm_3gpp_profile_get_password (b->priv->profile), flags))
        return FALSE;
    if (!(flags & MM_BEARER_PROPERTIES_CMP_FLAGS_NO_APN_TYPE) &&
        !cmp_apn_type (mm_3gpp_profile_get_apn_type (a->priv->profile), mm_3gpp_profile_get_apn_type (b->priv->profile), flags))
        return FALSE;
    if (!(flags & MM_BEARER_PROPERTIES_CMP_FLAGS_NO_PROFILE_ID) &&
        (mm_3gpp_profile_get_profile_id (a->priv->profile) != mm_3gpp_profile_get_profile_id (b->priv->profile)))
        return FALSE;
    if (!(flags & MM_BEARER_PROPERTIES_CMP_FLAGS_NO_PROFILE_NAME) &&
        !cmp_str (mm_3gpp_profile_get_profile_name (a->priv->profile), mm_3gpp_profile_get_profile_name (b->priv->profile), flags))
        return FALSE;
    if (!(flags & MM_BEARER_PROPERTIES_CMP_FLAGS_NO_ACCESS_TYPE_PREFERENCE) &&
        (mm_3gpp_profile_get_access_type_preference (a->priv->profile) != mm_3gpp_profile_get_access_type_preference (b->priv->profile)))
        return FALSE;
    if (!(flags & MM_BEARER_PROPERTIES_CMP_FLAGS_NO_ROAMING_ALLOWANCE) &&
        (mm_3gpp_profile_get_roaming_allowance (a->priv->profile) != mm_3gpp_profile_get_roaming_allowance (b->priv->profile)))
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
    if (a->priv->multiplex != b->priv->multiplex)
        return FALSE;
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_bearer_properties_print: (skip)
 */
GPtrArray *
mm_bearer_properties_print (MMBearerProperties *self,
                            gboolean            show_personal_info)
{
    GPtrArray   *array;
    const gchar *aux;

    array = mm_3gpp_profile_print (self->priv->profile, show_personal_info);
    if (self->priv->allow_roaming_set) {
        aux = mm_common_str_boolean (self->priv->allow_roaming);
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_ALLOW_ROAMING ": %s", aux));
    }
    if (self->priv->multiplex != MM_BEARER_MULTIPLEX_SUPPORT_UNKNOWN) {
        aux = mm_bearer_multiplex_support_get_string (self->priv->multiplex);
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_MULTIPLEX ": %s", aux));
    }
    if (self->priv->rm_protocol != MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
        aux = mm_modem_cdma_rm_protocol_get_string (self->priv->rm_protocol);
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_RM_PROTOCOL ": %s", aux));
    }
    return array;
}

/*****************************************************************************/

/**
 * mm_bearer_properties_new_from_profile: (skip)
 */
MMBearerProperties *
mm_bearer_properties_new_from_profile (MM3gppProfile  *profile,
                                       GError        **error)
{
    MMBearerProperties *self;

    self = mm_bearer_properties_new ();
    g_clear_object (&self->priv->profile);
    self->priv->profile = g_object_ref (profile);

    return self;
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
    self->priv->profile = mm_3gpp_profile_new ();
    self->priv->allow_roaming = TRUE;
    self->priv->rm_protocol = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
    self->priv->multiplex = MM_BEARER_MULTIPLEX_SUPPORT_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMBearerProperties *self = MM_BEARER_PROPERTIES (object);

    g_object_unref (self->priv->profile);

    G_OBJECT_CLASS (mm_bearer_properties_parent_class)->finalize (object);
}

static void
mm_bearer_properties_class_init (MMBearerPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerPropertiesPrivate));

    object_class->finalize = finalize;
}
