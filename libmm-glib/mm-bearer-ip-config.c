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
#include "mm-bearer-ip-config.h"

/**
 * SECTION: mm-bearer-ip-config
 * @title: MMBearerIpConfig
 * @short_description: Helper object to handle IP configuration.
 *
 * The #MMBearerIpConfig is an object handling the IP configuration required by
 * the bearer to finish the connection.
 *
 * This object is retrieved with either mm_bearer_get_ipv4_config(),
 * mm_bearer_peek_ipv4_config(), mm_bearer_get_ipv6_config() or
 * mm_bearer_peek_ipv6_config().
 */

G_DEFINE_TYPE (MMBearerIpConfig, mm_bearer_ip_config, G_TYPE_OBJECT);

#define PROPERTY_METHOD  "method"
#define PROPERTY_ADDRESS "address"
#define PROPERTY_PREFIX  "prefix"
#define PROPERTY_DNS1    "dns1"
#define PROPERTY_DNS2    "dns2"
#define PROPERTY_DNS3    "dns3"
#define PROPERTY_GATEWAY "gateway"
#define PROPERTY_MTU     "mtu"

struct _MMBearerIpConfigPrivate {
    MMBearerIpMethod method;
    gchar *address;
    guint prefix;
    gchar **dns;
    gchar *gateway;
    guint mtu;
};

/*****************************************************************************/

/**
 * mm_bearer_ip_config_get_method:
 * @self: a #MMBearerIpConfig.
 *
 * Gets the IP method to be used with this bearer.
 *
 * Returns: a #MMBearerIpMethod.
 */
MMBearerIpMethod
mm_bearer_ip_config_get_method (MMBearerIpConfig *self)
{
    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), MM_BEARER_IP_METHOD_UNKNOWN);

    return self->priv->method;
}

void
mm_bearer_ip_config_set_method (MMBearerIpConfig *self,
                                MMBearerIpMethod method)
{
    g_return_if_fail (MM_IS_BEARER_IP_CONFIG (self));

    self->priv->method = method;
}

/*****************************************************************************/

/**
 * mm_bearer_ip_config_get_address:
 * @self: a #MMBearerIpConfig.
 *
 * Gets the IP address to be used with this bearer.
 *
 * Returns: a string with the IP address, or #NULL if unknown. Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_bearer_ip_config_get_address (MMBearerIpConfig *self)
{
    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), NULL);

    return self->priv->address;
}

void
mm_bearer_ip_config_set_address (MMBearerIpConfig *self,
                                 const gchar *address)
{
    g_return_if_fail (MM_IS_BEARER_IP_CONFIG (self));

    g_free (self->priv->address);
    self->priv->address = g_strdup (address);
}

/*****************************************************************************/

/**
 * mm_bearer_ip_config_get_prefix:
 * @self: a #MMBearerIpConfig.
 *
 * Gets the network prefix to be used with this bearer.
 *
 * Returns: the network prefix.
 */
guint
mm_bearer_ip_config_get_prefix (MMBearerIpConfig *self)
{
    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), 0);

    return self->priv->prefix;
}

void
mm_bearer_ip_config_set_prefix (MMBearerIpConfig *self,
                                guint prefix)
{
    g_return_if_fail (MM_IS_BEARER_IP_CONFIG (self));

    self->priv->prefix = prefix;
}

/*****************************************************************************/

/**
 * mm_bearer_ip_config_get_dns:
 * @self: a #MMBearerIpConfig.
 *
 * Gets the list of IP addresses of DNS servers to be used with this bearer.
 *
 * Returns: (transfer none) (array zero-terminated=1): a NULL-terminated array of strings. Do not free the returned value, it is owned by @self.
 */
const gchar **
mm_bearer_ip_config_get_dns (MMBearerIpConfig *self)
{
    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), NULL);

    return (const gchar **)self->priv->dns;
}

void
mm_bearer_ip_config_set_dns (MMBearerIpConfig *self,
                             const gchar **dns)
{
    g_return_if_fail (MM_IS_BEARER_IP_CONFIG (self));

    g_strfreev (self->priv->dns);
    self->priv->dns = g_strdupv ((gchar **)dns);
}

/*****************************************************************************/

/**
 * mm_bearer_ip_config_get_gateway:
 * @self: a #MMBearerIpConfig.
 *
 * Gets the IP address of the gateway to be used with this bearer.
 *
 * Returns: a string with the IP address, or #NULL if unknown. Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_bearer_ip_config_get_gateway (MMBearerIpConfig *self)
{
    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), NULL);

    return self->priv->gateway;
}

void
mm_bearer_ip_config_set_gateway (MMBearerIpConfig *self,
                                 const gchar *gateway)
{
    g_return_if_fail (MM_IS_BEARER_IP_CONFIG (self));

    g_free (self->priv->gateway);
    self->priv->gateway = g_strdup (gateway);
}

/*****************************************************************************/

/**
 * mm_bearer_ip_config_get_mtu:
 * @self: a #MMBearerIpConfig.
 *
 * Gets the MTU to be used with this bearer.
 *
 * Returns: the MTU.
 */
guint
mm_bearer_ip_config_get_mtu (MMBearerIpConfig *self)
{
    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), 0);

    return self->priv->mtu;
}

void
mm_bearer_ip_config_set_mtu (MMBearerIpConfig *self,
                             guint mtu)
{
    g_return_if_fail (MM_IS_BEARER_IP_CONFIG (self));

    self->priv->mtu = mtu;
}

/*****************************************************************************/

GVariant *
mm_bearer_ip_config_get_dictionary (MMBearerIpConfig *self)
{
    GVariantBuilder builder;

    /* We do allow self==NULL. We'll just report method=unknown in this case */
    if (self)
        g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_METHOD,
                           g_variant_new_uint32 (self ?
                                                 self->priv->method :
                                                 MM_BEARER_IP_METHOD_UNKNOWN));
    if (self) {
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

        if (self->priv->mtu)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   PROPERTY_MTU,
                                   g_variant_new_uint32 (self->priv->mtu));
    }

    return g_variant_builder_end (&builder);
}

/*****************************************************************************/

MMBearerIpConfig *
mm_bearer_ip_config_new_from_dictionary (GVariant *dictionary,
                                         GError **error)
{
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMBearerIpConfig *self;
    gchar *dns_array[4] = { 0 };
    gboolean method_received = FALSE;

    self = mm_bearer_ip_config_new ();
    if (!dictionary)
        return self;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create IP config from dictionary: "
                     "invalid variant type received");
        g_object_unref (self);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        if (g_str_equal (key, PROPERTY_METHOD)) {
            method_received = TRUE;
            mm_bearer_ip_config_set_method (
                self,
                (MMBearerIpMethod) g_variant_get_uint32 (value));
        } else if (g_str_equal (key, PROPERTY_ADDRESS))
            mm_bearer_ip_config_set_address (
                self,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_PREFIX))
            mm_bearer_ip_config_set_prefix (
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
            mm_bearer_ip_config_set_gateway (
                self,
                g_variant_get_string (value, NULL));
        else if (g_str_equal (key, PROPERTY_MTU))
            mm_bearer_ip_config_set_mtu (
                self,
                g_variant_get_uint32 (value));

        g_free (key);
        g_variant_unref (value);
    }

    if (dns_array[0])
        mm_bearer_ip_config_set_dns (self, (const gchar **)dns_array);

    if (!method_received) {
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

/**
 * mm_bearer_ip_config_dup:
 * @orig: a #MMBearerIpConfig
 *
 * Creates a copy of @orig.
 *
 * Returns: (transfer full): a newly created #MMBearerIpConfig
 */
MMBearerIpConfig *
mm_bearer_ip_config_dup (MMBearerIpConfig *orig)
{
    GVariant *dict;
    MMBearerIpConfig *copy;
    GError *error = NULL;

    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (orig), NULL);

    dict = mm_bearer_ip_config_get_dictionary (orig);
    copy = mm_bearer_ip_config_new_from_dictionary (dict, &error);
    g_assert_no_error (error);
    g_variant_unref (dict);

    return copy;
}

/*****************************************************************************/

MMBearerIpConfig *
mm_bearer_ip_config_new (void)
{
    return (MM_BEARER_IP_CONFIG (
                g_object_new (MM_TYPE_BEARER_IP_CONFIG, NULL)));
}

static void
mm_bearer_ip_config_init (MMBearerIpConfig *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BEARER_IP_CONFIG,
                                              MMBearerIpConfigPrivate);

    /* Some defaults */
    self->priv->method = MM_BEARER_IP_METHOD_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMBearerIpConfig *self = MM_BEARER_IP_CONFIG (object);

    g_free (self->priv->address);
    g_free (self->priv->gateway);
    g_strfreev (self->priv->dns);

    G_OBJECT_CLASS (mm_bearer_ip_config_parent_class)->finalize (object);
}

static void
mm_bearer_ip_config_class_init (MMBearerIpConfigClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerIpConfigPrivate));

    object_class->finalize = finalize;
}
