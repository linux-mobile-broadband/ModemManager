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
 * Copyright (C) 2012 Google, Inc.
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-common-bearer-ip-config.h"

G_DEFINE_TYPE (MMCommonBearerIpConfig, mm_common_bearer_ip_config, G_TYPE_OBJECT);

#define PROPERTY_METHOD  "method"
#define PROPERTY_ADDRESS "address"
#define PROPERTY_PREFIX  "prefix"
#define PROPERTY_DNS1    "dns1"
#define PROPERTY_DNS2    "dns2"
#define PROPERTY_DNS3    "dns3"
#define PROPERTY_GATEWAY "gateway"

struct _MMCommonBearerIpConfigPrivate {
    MMBearerIpMethod method;
    gchar *address;
    guint prefix;
    gchar **dns;
    gchar *gateway;
};

/*****************************************************************************/

void
mm_common_bearer_ip_config_set_method (MMCommonBearerIpConfig *self,
                                       MMBearerIpMethod method)
{
    self->priv->method = method;
}

void
mm_common_bearer_ip_config_set_address (MMCommonBearerIpConfig *self,
                                        const gchar *address)
{
    g_free (self->priv->address);
    self->priv->address = g_strdup (address);
}

void
mm_common_bearer_ip_config_set_prefix (MMCommonBearerIpConfig *self,
                                       guint prefix)
{
    self->priv->prefix = prefix;
}

void
mm_common_bearer_ip_config_set_dns (MMCommonBearerIpConfig *self,
                                    const gchar **dns)
{
    g_strfreev (self->priv->dns);
    self->priv->dns = g_strdupv ((gchar **)dns);
}

void
mm_common_bearer_ip_config_set_gateway (MMCommonBearerIpConfig *self,
                                        const gchar *gateway)
{
    g_free (self->priv->gateway);
    self->priv->gateway = g_strdup (gateway);
}

/*****************************************************************************/

MMBearerIpMethod
mm_common_bearer_ip_config_get_method (MMCommonBearerIpConfig *self)
{
    return self->priv->method;
}

const gchar *
mm_common_bearer_ip_config_get_address (MMCommonBearerIpConfig *self)
{
    return self->priv->address;
}

guint
mm_common_bearer_ip_config_get_prefix (MMCommonBearerIpConfig *self)
{
    return self->priv->prefix;
}

const gchar **
mm_common_bearer_ip_config_get_dns (MMCommonBearerIpConfig *self)
{
    return (const gchar **)self->priv->dns;
}

const gchar *
mm_common_bearer_ip_config_get_gateway (MMCommonBearerIpConfig *self)
{
    return self->priv->gateway;
}

/*****************************************************************************/

GVariant *
mm_common_bearer_ip_config_get_dictionary (MMCommonBearerIpConfig *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_METHOD,
                           g_variant_new_uint32 (self->priv->method));

    /* If static IP method, report remaining configuration */
    if (self->priv->method == MM_BEARER_IP_METHOD_STATIC) {
        if (self->priv->address)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   PROPERTY_ADDRESS,
                                   g_variant_new_string (self->priv->address));

        if (self->priv->prefix)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   PROPERTY_PREFIX,
                                   g_variant_new_uint32 (self->priv->prefix));

        if (self->priv->dns &&
            self->priv->dns[0]) {
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   PROPERTY_DNS1,
                                   g_variant_new_string (self->priv->dns[0]));
            if (self->priv->dns[1]) {
                g_variant_builder_add (&builder,
                                       "{sv}",
                                       PROPERTY_DNS2,
                                       g_variant_new_string (self->priv->dns[1]));
                    if (self->priv->dns[2]) {
                        g_variant_builder_add (&builder,
                                               "{sv}",
                                               PROPERTY_DNS3,
                                               g_variant_new_string (self->priv->dns[2]));
                    }
            }
        }

        if (self->priv->gateway)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   PROPERTY_GATEWAY,
                                   g_variant_new_string (self->priv->gateway));
    }

    return g_variant_builder_end (&builder);
}

/*****************************************************************************/

MMCommonBearerIpConfig *
mm_common_bearer_ip_config_new_from_dictionary (GVariant *dictionary,
                                                GError **error)
{
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMCommonBearerIpConfig *self;
    gchar *dns_array[4] = { 0 };

    self = mm_common_bearer_ip_config_new ();
    if (!dictionary)
        return self;

    g_variant_iter_init (&iter, dictionary);
    while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        if (g_str_equal (key, PROPERTY_METHOD))
            mm_common_bearer_ip_config_set_method (
                self,
                (MMBearerIpMethod) g_variant_get_uint32 (value));
        else if (g_str_equal (key, PROPERTY_ADDRESS))
            mm_common_bearer_ip_config_set_address (
                self,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_PREFIX))
            mm_common_bearer_ip_config_set_prefix (
                self,
                g_variant_get_uint32 (value));
        else if (g_str_equal (key, PROPERTY_DNS1)) {
            g_free (dns_array[0]);
            dns_array[0] = g_variant_dup_string (value, NULL);
        } else if (g_str_equal (key, PROPERTY_DNS2)) {
            g_free (dns_array[1]);
            dns_array[1] = g_variant_dup_string (value, NULL);
        } else if (g_str_equal (key, PROPERTY_DNS3)) {
            g_free (dns_array[2]);
            dns_array[2] = g_variant_dup_string (value, NULL);
        } else if (g_str_equal (key, PROPERTY_GATEWAY))
            mm_common_bearer_ip_config_set_gateway (
                self,
                g_variant_get_string (value, NULL));

        g_free (key);
        g_variant_unref (value);
    }

    if (dns_array[0])
        mm_common_bearer_ip_config_set_dns (self, (const gchar **)dns_array);

    if (self->priv->method == MM_BEARER_IP_METHOD_UNKNOWN) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Couldn't create IP config from dictionary: 'method not given'");
        g_clear_object (&self);
    }

    g_free (dns_array[0]);
    g_free (dns_array[1]);
    g_free (dns_array[2]);

    return self;
}

/*****************************************************************************/

MMCommonBearerIpConfig *
mm_common_bearer_ip_config_dup (MMCommonBearerIpConfig *orig)
{
    GVariant *dict;
    MMCommonBearerIpConfig *copy;
    GError *error = NULL;

    dict = mm_common_bearer_ip_config_get_dictionary (orig);
    copy = mm_common_bearer_ip_config_new_from_dictionary (dict, &error);
    g_assert_no_error (error);
    g_variant_unref (dict);

    return copy;
}

/*****************************************************************************/

MMCommonBearerIpConfig *
mm_common_bearer_ip_config_new (void)
{
    return (MM_COMMON_BEARER_IP_CONFIG (
                g_object_new (MM_TYPE_COMMON_BEARER_IP_CONFIG, NULL)));
}

static void
mm_common_bearer_ip_config_init (MMCommonBearerIpConfig *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_COMMON_BEARER_IP_CONFIG,
                                              MMCommonBearerIpConfigPrivate);

    /* Some defaults */
    self->priv->method = MM_BEARER_IP_METHOD_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMCommonBearerIpConfig *self = MM_COMMON_BEARER_IP_CONFIG (object);

    g_free (self->priv->address);
    g_free (self->priv->gateway);
    g_strfreev (self->priv->dns);

    G_OBJECT_CLASS (mm_common_bearer_ip_config_parent_class)->finalize (object);
}

static void
mm_common_bearer_ip_config_class_init (MMCommonBearerIpConfigClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCommonBearerIpConfigPrivate));

    object_class->finalize = finalize;
}
