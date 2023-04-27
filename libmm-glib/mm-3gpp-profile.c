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
 * Copyright (C) 2021-2022 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2022 Google, Inc.
 */

#include <string.h>

#include "mm-enums-types.h"
#include "mm-flags-types.h"
#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-3gpp-profile.h"

/**
 * SECTION: mm-3gpp-profile
 * @title: MM3gppProfile
 * @short_description: Helper object to handle 3GPP profile settings.
 *
 * The #MM3gppProfile is an object handling the settings associated
 * to a connection profile stored in the device.
 */

G_DEFINE_TYPE (MM3gppProfile, mm_3gpp_profile, G_TYPE_OBJECT)

#define PROPERTY_ID                     "profile-id"
#define PROPERTY_NAME                   "profile-name"
#define PROPERTY_APN                    "apn"
#define PROPERTY_ALLOWED_AUTH           "allowed-auth"
#define PROPERTY_USER                   "user"
#define PROPERTY_PASSWORD               "password"
#define PROPERTY_IP_TYPE                "ip-type"
#define PROPERTY_APN_TYPE               "apn-type"
#define PROPERTY_ACCESS_TYPE_PREFERENCE "access-type-preference"
#define PROPERTY_ENABLED                "profile-enabled"
#define PROPERTY_ROAMING_ALLOWANCE      "roaming-allowance"
#define PROPERTY_SOURCE                 "profile-source"

struct _MM3gppProfilePrivate {
    gint                          profile_id;
    gchar                        *profile_name;
    gchar                        *apn;
    MMBearerIpFamily              ip_type;
    MMBearerApnType               apn_type;
    MMBearerAccessTypePreference  access_type_preference;
    gboolean                      enabled;
    gboolean                      enabled_set;
    MMBearerRoamingAllowance      roaming_allowance;
    MMBearerProfileSource         profile_source;

    /* Optional authentication settings */
    MMBearerAllowedAuth  allowed_auth;
    gchar               *user;
    gchar               *password;
};

/*****************************************************************************/

static gboolean
cmp_str (const gchar *a,
         const gchar *b)
{
    /* Strict match */
    if ((!a && !b) || (a && b && g_strcmp0 (a, b) == 0))
        return TRUE;
    /* Additional match, consider NULL and EMPTY string equal */
    if ((!a && !b[0]) || (!b && !a[0]))
        return TRUE;
    return FALSE;
}

/**
 * mm_3gpp_profile_cmp: (skip)
 */
gboolean
mm_3gpp_profile_cmp (MM3gppProfile         *a,
                     MM3gppProfile         *b,
                     GEqualFunc             cmp_apn,
                     MM3gppProfileCmpFlags  flags)
{
    /* When an input cmp_apn() methods is provided to compare the APNs, we must
     * run it twice, with the input arguments switched, as e.g. the mm_3gpp_cmp_apn_name()
     * method that may be given here treats both input arguments differently. */
    if (cmp_apn && !cmp_apn (a->priv->apn, b->priv->apn) && !cmp_apn (b->priv->apn, a->priv->apn))
        return FALSE;
    if (!cmp_apn && !cmp_str (a->priv->apn, b->priv->apn))
        return FALSE;
    if (!(flags & MM_3GPP_PROFILE_CMP_FLAGS_NO_IP_TYPE) &&
        (a->priv->ip_type != b->priv->ip_type))
        return FALSE;
    if (!(flags & MM_3GPP_PROFILE_CMP_FLAGS_NO_PROFILE_ID) &&
        (a->priv->profile_id != b->priv->profile_id))
        return FALSE;
    if (!(flags & MM_3GPP_PROFILE_CMP_FLAGS_NO_AUTH) &&
        ((a->priv->allowed_auth != b->priv->allowed_auth) ||
         (!cmp_str (a->priv->user, b->priv->user)) ||
         (!cmp_str (a->priv->password, b->priv->password))))
        return FALSE;
    if (!(flags & MM_3GPP_PROFILE_CMP_FLAGS_NO_APN_TYPE) &&
        (a->priv->apn_type != b->priv->apn_type))
        return FALSE;
    if (!(flags & MM_3GPP_PROFILE_CMP_FLAGS_NO_PROFILE_NAME) &&
        (a->priv->profile_name != b->priv->profile_name))
        return FALSE;
    if (!(flags & MM_3GPP_PROFILE_CMP_FLAGS_NO_ACCESS_TYPE_PREFERENCE) &&
        (a->priv->access_type_preference != b->priv->access_type_preference))
        return FALSE;
    if (!(flags & MM_3GPP_PROFILE_CMP_FLAGS_NO_ENABLED) &&
        ((a->priv->enabled != b->priv->enabled) || (a->priv->enabled_set != b->priv->enabled_set)))
        return FALSE;
    if (!(flags & MM_3GPP_PROFILE_CMP_FLAGS_NO_ROAMING_ALLOWANCE) &&
        (a->priv->roaming_allowance != b->priv->roaming_allowance))
        return FALSE;
    if (!(flags & MM_3GPP_PROFILE_CMP_FLAGS_NO_PROFILE_SOURCE) &&
        (a->priv->profile_source != b->priv->profile_source))
        return FALSE;

    return TRUE;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_profile_id:
 * @self: a #MM3gppProfile.
 * @profile_id: Numeric profile id to use, or #MM_3GPP_PROFILE_ID_UNKNOWN.
 *
 * Sets the profile id to use.
 *
 * If none specified explicitly, #MM_3GPP_PROFILE_ID_UNKNOWN is assumed.
 *
 * Since: 1.18
 */
void
mm_3gpp_profile_set_profile_id (MM3gppProfile *self,
                                gint           profile_id)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    self->priv->profile_id = profile_id;
}

/**
 * mm_3gpp_profile_get_profile_id:
 * @self: a #MM3gppProfile.
 *
 * Gets the profile id.
 *
 * Returns: the profile id..
 *
 * Since: 1.18
 */
gint
mm_3gpp_profile_get_profile_id (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), MM_3GPP_PROFILE_ID_UNKNOWN);

    return self->priv->profile_id;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_apn:
 * @self: a #MM3gppProfile.
 * @apn: Name of the access point.
 *
 * Sets the name of the access point to use.
 *
 * Since: 1.18
 */
void
mm_3gpp_profile_set_apn (MM3gppProfile *self,
                         const gchar   *apn)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    g_free (self->priv->apn);
    self->priv->apn = g_strdup (apn);
}

/**
 * mm_3gpp_profile_get_apn:
 * @self: a #MM3gppProfile.
 *
 * Gets the name of the access point.
 *
 * Returns: (transfer none): the access point, or #NULL if not set. Do not free
 * the returned value, it is owned by @self.
 *
 * Since: 1.18
 */
const gchar *
mm_3gpp_profile_get_apn (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), NULL);

    return self->priv->apn;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_profile_name:
 * @self: a #MM3gppProfile.
 * @profile_name: Name of the profile.
 *
 * Sets the name of the profile.
 *
 * Since: 1.20
 */
void
mm_3gpp_profile_set_profile_name (MM3gppProfile *self,
                                  const gchar   *profile_name)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    g_free (self->priv->profile_name);
    self->priv->profile_name = g_strdup (profile_name);
}

/**
 * mm_3gpp_profile_get_profile_name:
 * @self: a #MM3gppProfile.
 *
 * Gets the name of the profile.
 *
 * Returns: (transfer none): the profile name, or #NULL if not set. Do not free
 * the returned value, it is owned by @self.
 *
 * Since: 1.20
 */
const gchar *
mm_3gpp_profile_get_profile_name (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), NULL);

    return self->priv->profile_name;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_allowed_auth:
 * @self: a #MM3gppProfile.
 * @allowed_auth: a bitmask of #MMBearerAllowedAuth values.
 *  %MM_BEARER_ALLOWED_AUTH_UNKNOWN may be given to request the modem-default
 *  method.
 *
 * Sets the method to use when authenticating with the access point.
 *
 * Since: 1.18
 */
void
mm_3gpp_profile_set_allowed_auth (MM3gppProfile       *self,
                                  MMBearerAllowedAuth  allowed_auth)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    self->priv->allowed_auth = allowed_auth;
}

/**
 * mm_3gpp_profile_get_allowed_auth:
 * @self: a #MM3gppProfile.
 *
 * Gets the methods allowed to use when authenticating with the access point.
 *
 * Returns: a bitmask of #MMBearerAllowedAuth values, or
 * %MM_BEARER_ALLOWED_AUTH_UNKNOWN to request the modem-default method.
 *
 * Since: 1.18
 */
MMBearerAllowedAuth
mm_3gpp_profile_get_allowed_auth (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), MM_BEARER_ALLOWED_AUTH_UNKNOWN);

    return self->priv->allowed_auth;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_user:
 * @self: a #MM3gppProfile.
 * @user: the username
 *
 * Sets the username used to authenticate with the access point.
 *
 * Since: 1.18
 */
void
mm_3gpp_profile_set_user (MM3gppProfile *self,
                          const gchar   *user)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    g_free (self->priv->user);
    self->priv->user = g_strdup (user);
}

/**
 * mm_3gpp_profile_get_user:
 * @self: a #MM3gppProfile.
 *
 * Gets the username used to authenticate with the access point.
 *
 * Returns: (transfer none): the username, or #NULL if not set. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.18
 */
const gchar *
mm_3gpp_profile_get_user (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), NULL);

    return self->priv->user;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_password:
 * @self: a #MM3gppProfile.
 * @password: the password
 *
 * Sets the password used to authenticate with the access point.
 *
 * Since: 1.18
 */
void
mm_3gpp_profile_set_password (MM3gppProfile *self,
                              const gchar   *password)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    g_free (self->priv->password);
    self->priv->password = g_strdup (password);
}

/**
 * mm_3gpp_profile_get_password:
 * @self: a #MM3gppProfile.
 *
 * Gets the password used to authenticate with the access point.
 *
 * Returns: (transfer none): the password, or #NULL if not set. Do not free
 * the returned value, it is owned by @self.
 *
 * Since: 1.18
 */
const gchar *
mm_3gpp_profile_get_password (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), NULL);

    return self->priv->password;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_ip_type:
 * @self: a #MM3gppProfile.
 * @ip_type: a #MMBearerIpFamily.
 *
 * Sets the IP type to use.
 *
 * Since: 1.18
 */
void
mm_3gpp_profile_set_ip_type (MM3gppProfile    *self,
                             MMBearerIpFamily  ip_type)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    self->priv->ip_type = ip_type;
}

/**
 * mm_3gpp_profile_get_ip_type:
 * @self: a #MM3gppProfile.
 *
 * Gets the IP type to use.
 *
 * Returns: a #MMBearerIpFamily.
 *
 * Since: 1.18
 */
MMBearerIpFamily
mm_3gpp_profile_get_ip_type (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), MM_BEARER_IP_FAMILY_NONE);

    return self->priv->ip_type;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_apn_type:
 * @self: a #MM3gppProfile.
 * @apn_type: a mask of #MMBearerApnType values.
 *
 * Sets the APN types to use.
 *
 * Since: 1.18
 */
void
mm_3gpp_profile_set_apn_type (MM3gppProfile   *self,
                              MMBearerApnType  apn_type)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    self->priv->apn_type = apn_type;
}

/**
 * mm_3gpp_profile_get_apn_type:
 * @self: a #MM3gppProfile.
 *
 * Gets the APN types to use.
 *
 * Returns: a mask of #MMBearerApnType values.
 *
 * Since: 1.18
 */
MMBearerApnType
mm_3gpp_profile_get_apn_type (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), MM_BEARER_APN_TYPE_NONE);

    return self->priv->apn_type;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_access_type_preference:
 * @self: a #MM3gppProfile.
 * @access_type_preference: a #MMBearerAccessTypePreference.
 *
 * Sets the 5G network access type preference.
 *
 * Since: 1.20
 */
void
mm_3gpp_profile_set_access_type_preference (MM3gppProfile                *self,
                                            MMBearerAccessTypePreference  access_type_preference)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    self->priv->access_type_preference = access_type_preference;
}

/**
 * mm_3gpp_profile_get_access_type_preference:
 * @self: a #MM3gppProfile.
 *
 * Gets 5G network access type preference.
 *
 * Returns: a #MMBearerAccessTypePreference.
 *
 * Since: 1.20
 */
MMBearerAccessTypePreference
mm_3gpp_profile_get_access_type_preference (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), MM_BEARER_ACCESS_TYPE_PREFERENCE_NONE);

    return self->priv->access_type_preference;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_enabled:
 * @self: a #MM3gppProfile.
 * @enabled: boolean value.
 *
 * Sets the flag to indicate whether the profile is enabled or disabled.
 *
 * Since: 1.20
 */
void
mm_3gpp_profile_set_enabled (MM3gppProfile *self,
                             gboolean       enabled)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    self->priv->enabled_set = TRUE;
    self->priv->enabled = enabled;
}

/**
 * mm_3gpp_profile_get_enabled:
 * @self: a #MM3gppProfile.
 *
 * Checks whether the profile is enabled or disabled.
 *
 * Returns: %TRUE if the profile is enabled, %FALSE otherwise.
 *
 * Since: 1.20
 */
gboolean
mm_3gpp_profile_get_enabled (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), FALSE);

    return self->priv->enabled;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_roaming_allowance:
 * @self: a #MM3gppProfile.
 * @roaming_allowance: a mask of #MMBearerRoamingAllowance values.
 *
 * Sets the roaming allowance rules.
 *
 * Since: 1.20
 */
void
mm_3gpp_profile_set_roaming_allowance (MM3gppProfile            *self,
                                       MMBearerRoamingAllowance  roaming_allowance)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    self->priv->roaming_allowance = roaming_allowance;
}

/**
 * mm_3gpp_profile_get_roaming_allowance:
 * @self: a #MM3gppProfile.
 *
 * Gets the roaming allowance rules.
 *
 * Returns: a mask of #MMBearerRoamingAllowance values.
 *
 * Since: 1.20
 */
MMBearerRoamingAllowance
mm_3gpp_profile_get_roaming_allowance (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), MM_BEARER_ROAMING_ALLOWANCE_NONE);

    return self->priv->roaming_allowance;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_set_profile_source:
 * @self: a #MM3gppProfile.
 * @profile_source: a #MMBearerProfileSource.
 *
 * Sets profile source.
 *
 * Since: 1.20
 */
void
mm_3gpp_profile_set_profile_source (MM3gppProfile         *self,
                                    MMBearerProfileSource  profile_source)
{
    g_return_if_fail (MM_IS_3GPP_PROFILE (self));

    self->priv->profile_source = profile_source;
}

/**
 * mm_3gpp_profile_get_profile_source:
 * @self: a #MM3gppProfile.
 *
 * Gets the profile source.
 *
 * Returns: a #MMBearerProfileSource.
 *
 * Since: 1.20
 */
MMBearerProfileSource
mm_3gpp_profile_get_profile_source (MM3gppProfile *self)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), MM_BEARER_PROFILE_SOURCE_UNKNOWN);

    return self->priv->profile_source;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_get_dictionary: (skip)
 */
GVariant *
mm_3gpp_profile_get_dictionary (MM3gppProfile *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_ID,
                           g_variant_new_int32 (self->priv->profile_id));

    if (self->priv->profile_name)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_NAME,
                               g_variant_new_string (self->priv->profile_name));

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

    if (self->priv->apn_type != MM_BEARER_APN_TYPE_NONE)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_APN_TYPE,
                               g_variant_new_uint32 (self->priv->apn_type));

    if (self->priv->access_type_preference != MM_BEARER_ACCESS_TYPE_PREFERENCE_NONE)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ACCESS_TYPE_PREFERENCE,
                               g_variant_new_uint32 (self->priv->access_type_preference));

    if (self->priv->enabled_set)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ENABLED,
                               g_variant_new_boolean (self->priv->enabled));


    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

gboolean
mm_3gpp_profile_consume_string (MM3gppProfile  *self,
                                const gchar    *key,
                                const gchar    *value,
                                GError        **error)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), FALSE);

    if (g_str_equal (key, PROPERTY_ID)) {
        gint profile_id;

        if (!mm_get_int_from_str (value, &profile_id)) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                         "invalid profile id value given: %s", value);
            return FALSE;
        }
        mm_3gpp_profile_set_profile_id (self, profile_id);
    } else if (g_str_equal (key, PROPERTY_NAME))
        mm_3gpp_profile_set_profile_name (self, value);
    else if (g_str_equal (key, PROPERTY_APN))
        mm_3gpp_profile_set_apn (self, value);
    else if (g_str_equal (key, PROPERTY_ALLOWED_AUTH)) {
        GError              *inner_error = NULL;
        MMBearerAllowedAuth  allowed_auth;

        allowed_auth = mm_common_get_allowed_auth_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_3gpp_profile_set_allowed_auth (self, allowed_auth);
    } else if (g_str_equal (key, PROPERTY_USER))
        mm_3gpp_profile_set_user (self, value);
    else if (g_str_equal (key, PROPERTY_PASSWORD))
        mm_3gpp_profile_set_password (self, value);
    else if (g_str_equal (key, PROPERTY_IP_TYPE)) {
        GError           *inner_error = NULL;
        MMBearerIpFamily  ip_type;

        ip_type = mm_common_get_ip_type_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_3gpp_profile_set_ip_type (self, ip_type);
    } else if (g_str_equal (key, PROPERTY_APN_TYPE)) {
        GError          *inner_error = NULL;
        MMBearerApnType  apn_type;

        apn_type = mm_common_get_apn_type_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_3gpp_profile_set_apn_type (self, apn_type);
    } else if (g_str_equal (key, PROPERTY_ACCESS_TYPE_PREFERENCE)) {
        GError                       *inner_error = NULL;
        MMBearerAccessTypePreference  access_type_preference;

        access_type_preference = mm_common_get_access_type_preference_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_3gpp_profile_set_access_type_preference (self, access_type_preference);
    } else if (g_str_equal (key, PROPERTY_ENABLED)) {
        GError   *inner_error = NULL;
        gboolean  profile_enabled;

        profile_enabled = mm_common_get_boolean_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_3gpp_profile_set_enabled (self, profile_enabled);
    } else if (g_str_equal (key, PROPERTY_SOURCE)) {
        GError                *inner_error = NULL;
        MMBearerProfileSource  profile_source;

        profile_source = mm_common_get_profile_source_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_3gpp_profile_set_profile_source (self, profile_source);
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
    MM3gppProfile *properties;
    GError        *error;
} ParseKeyValueContext;

static gboolean
key_value_foreach (const gchar          *key,
                   const gchar          *value,
                   ParseKeyValueContext *ctx)
{
    return mm_3gpp_profile_consume_string (ctx->properties,
                                           key,
                                           value,
                                           &ctx->error);
}

/**
 * mm_3gpp_profile_new_from_string: (skip)
 */
MM3gppProfile *
mm_3gpp_profile_new_from_string (const gchar  *str,
                                 GError      **error)
{
    ParseKeyValueContext ctx;

    ctx.error = NULL;
    ctx.properties = mm_3gpp_profile_new ();

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

gboolean
mm_3gpp_profile_consume_variant (MM3gppProfile  *self,
                                 const gchar    *key,
                                 GVariant       *value,
                                 GError        **error)
{
    g_return_val_if_fail (MM_IS_3GPP_PROFILE (self), FALSE);

    if (g_str_equal (key, PROPERTY_ID))
        mm_3gpp_profile_set_profile_id (
            self,
            g_variant_get_int32 (value));
    else if (g_str_equal (key, PROPERTY_NAME))
        mm_3gpp_profile_set_profile_name (
            self,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_APN))
        mm_3gpp_profile_set_apn (
            self,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_ALLOWED_AUTH))
        mm_3gpp_profile_set_allowed_auth (
            self,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_USER))
        mm_3gpp_profile_set_user (
            self,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_PASSWORD))
        mm_3gpp_profile_set_password (
            self,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_IP_TYPE))
        mm_3gpp_profile_set_ip_type (
            self,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_APN_TYPE))
        mm_3gpp_profile_set_apn_type (
            self,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_ACCESS_TYPE_PREFERENCE))
        mm_3gpp_profile_set_access_type_preference (
            self,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_ENABLED))
        mm_3gpp_profile_set_enabled (
            self,
            g_variant_get_boolean (value));
    else if (g_str_equal (key, PROPERTY_SOURCE))
        mm_3gpp_profile_set_profile_source (
            self,
            g_variant_get_uint32 (value));
    else {
        /* Set error */
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid self dictionary, unexpected key '%s'",
                     key);
        return FALSE;
    }

    return TRUE;
}

/**
 * mm_3gpp_profile_new_from_dictionary: (skip)
 */
MM3gppProfile *
mm_3gpp_profile_new_from_dictionary (GVariant  *dictionary,
                                     GError   **error)
{
    GError        *inner_error = NULL;
    GVariantIter   iter;
    gchar         *key;
    GVariant      *value;
    MM3gppProfile *properties;

    properties = mm_3gpp_profile_new ();
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
        mm_3gpp_profile_consume_variant (properties, key, value, &inner_error);
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
 * mm_3gpp_profile_print: (skip)
 */
GPtrArray *
mm_3gpp_profile_print (MM3gppProfile *self,
                       gboolean       show_personal_info)
{
    GPtrArray        *array;
    g_autofree gchar *ip_type_str = NULL;
    g_autofree gchar *apn_type_str = NULL;
    g_autofree gchar *roaming_allowance_str = NULL;
    g_autofree gchar *allowed_auth_str = NULL;
    const gchar      *aux;

    array = g_ptr_array_new_with_free_func ((GDestroyNotify)g_free);
    if (self->priv->profile_id != MM_3GPP_PROFILE_ID_UNKNOWN)
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_ID ": %d", self->priv->profile_id));
    if (self->priv->profile_name)
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_NAME ": %s", self->priv->profile_name));
    if (self->priv->enabled_set) {
        aux = mm_common_str_boolean (self->priv->enabled);
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_ENABLED ": %s", aux));
    }
    if (self->priv->apn)
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_APN ": %s", self->priv->apn));
    if (self->priv->ip_type != MM_BEARER_IP_FAMILY_NONE) {
        ip_type_str = mm_bearer_ip_family_build_string_from_mask (self->priv->ip_type);
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_IP_TYPE ": %s", ip_type_str));
    }
    if (self->priv->apn_type != MM_BEARER_APN_TYPE_NONE) {
        apn_type_str = mm_bearer_apn_type_build_string_from_mask (self->priv->apn_type);
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_APN_TYPE ": %s", apn_type_str));
    }
    if (self->priv->access_type_preference != MM_BEARER_ACCESS_TYPE_PREFERENCE_NONE) {
        aux = mm_bearer_access_type_preference_get_string (self->priv->access_type_preference);
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_ACCESS_TYPE_PREFERENCE ": %s", aux));
    }
    if (self->priv->roaming_allowance != MM_BEARER_ROAMING_ALLOWANCE_NONE) {
        roaming_allowance_str = mm_bearer_roaming_allowance_build_string_from_mask (self->priv->roaming_allowance);
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_ROAMING_ALLOWANCE ": %s", roaming_allowance_str));
    }
    if (self->priv->profile_source != MM_BEARER_PROFILE_SOURCE_UNKNOWN) {
        aux = mm_bearer_profile_source_get_string (self->priv->profile_source);
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_SOURCE ": %s", aux));
    }
    if (self->priv->allowed_auth != MM_BEARER_ALLOWED_AUTH_NONE) {
        allowed_auth_str = mm_bearer_allowed_auth_build_string_from_mask (self->priv->allowed_auth);
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_ALLOWED_AUTH ": %s", allowed_auth_str));
    }
    if (self->priv->user)
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_USER ": %s", mm_common_str_personal_info (self->priv->user, show_personal_info)));
    if (self->priv->password)
        g_ptr_array_add (array, g_strdup_printf (PROPERTY_PASSWORD ": %s", mm_common_str_personal_info (self->priv->password, show_personal_info)));
    return array;
}

/*****************************************************************************/

/**
 * mm_3gpp_profile_new:
 *
 * Creates a new empty #MM3gppProfile.
 *
 * Returns: (transfer full): a #MM3gppProfile. The returned value should be freed with g_object_unref().
 *
 * Since: 1.18
 */
MM3gppProfile *
mm_3gpp_profile_new (void)
{
    return MM_3GPP_PROFILE (g_object_new (MM_TYPE_3GPP_PROFILE, NULL));
}

static void
mm_3gpp_profile_init (MM3gppProfile *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_3GPP_PROFILE, MM3gppProfilePrivate);

    /* Some defaults */
    self->priv->profile_id = MM_3GPP_PROFILE_ID_UNKNOWN;
    self->priv->allowed_auth = MM_BEARER_ALLOWED_AUTH_UNKNOWN;
    self->priv->ip_type = MM_BEARER_IP_FAMILY_NONE;
    self->priv->apn_type = MM_BEARER_APN_TYPE_NONE;
    self->priv->access_type_preference = MM_BEARER_ACCESS_TYPE_PREFERENCE_NONE;
    self->priv->enabled = TRUE;
    self->priv->roaming_allowance = MM_BEARER_ROAMING_ALLOWANCE_NONE;
    self->priv->profile_source = MM_BEARER_PROFILE_SOURCE_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MM3gppProfile *self = MM_3GPP_PROFILE (object);

    g_free (self->priv->profile_name);
    g_free (self->priv->apn);
    g_free (self->priv->user);
    g_free (self->priv->password);

    G_OBJECT_CLASS (mm_3gpp_profile_parent_class)->finalize (object);
}

static void
mm_3gpp_profile_class_init (MM3gppProfileClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MM3gppProfilePrivate));

    object_class->finalize = finalize;
}
