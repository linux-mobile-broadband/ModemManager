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
 * Copyright (C) 2024 Google, Inc.
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-network-rejection.h"

/**
 * SECTION: mm-network-rejection
 * @title: MMNetworkRejection
 * @short_description: Helper object to handle network rejection.
 *
 * The #MMNetworkRejection is an object handling the network rejection
 * related information.
 *
 * This object is retrieved with either mm_modem_3gpp_get_network_rejection(),
 * or mm_modem_3gpp_peek_network_rejection().
 */

G_DEFINE_TYPE (MMNetworkRejection, mm_network_rejection, G_TYPE_OBJECT)

#define PROPERTY_ERROR             "error"
#define PROPERTY_OPERATOR_ID       "operator-id"
#define PROPERTY_OPERATOR_NAME     "operator-name"
#define PROPERTY_ACCESS_TECHNOLOGY "access-technology"

struct _MMNetworkRejectionPrivate {
    gchar                   *operator_id;
    gchar                   *operator_name;
    MMModemAccessTechnology  access_technology;
    MMNetworkError           error;
};

/*****************************************************************************/

/**
 * mm_network_rejection_get_operator_id:
 * @self: a #MMNetworkRejection.
 *
 * Gets the operator id reported with network reject.
 *
 * Returns: a string with the operator id, or #NULL if unknown. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.24
 */
const gchar *
mm_network_rejection_get_operator_id (MMNetworkRejection *self)
{
    g_return_val_if_fail (MM_IS_NETWORK_REJECTION (self), NULL);

    return self->priv->operator_id;
}

/**
 * mm_network_rejection_set_operator_id: (skip)
 */
void
mm_network_rejection_set_operator_id (MMNetworkRejection *self,
                                      const gchar        *operator_id)
{
    g_return_if_fail (MM_IS_NETWORK_REJECTION (self));

    g_free (self->priv->operator_id);
    self->priv->operator_id = g_strdup (operator_id);
}

/*****************************************************************************/

/**
 * mm_network_rejection_get_operator_name:
 * @self: a #MMNetworkRejection.
 *
 * Gets the operator name reported with network reject.
 *
 * Returns: a string with the operator name, or #NULL if unknown. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.24
 */
const gchar *
mm_network_rejection_get_operator_name (MMNetworkRejection *self)
{
    g_return_val_if_fail (MM_IS_NETWORK_REJECTION (self), NULL);

    return self->priv->operator_name;
}

/**
 * mm_network_rejection_set_operator_name: (skip)
 */
void
mm_network_rejection_set_operator_name (MMNetworkRejection *self,
                                        const gchar        *operator_name)
{
    g_return_if_fail (MM_IS_NETWORK_REJECTION (self));

    g_free (self->priv->operator_name);
    self->priv->operator_name = g_strdup (operator_name);
}

/*****************************************************************************/

/**
 * mm_network_rejection_get_error:
 * @self: a #MMNetworkRejection.
 *
 * Gets the network error reported with network reject.
 *
 * Returns: the network error.
 *
 * Since: 1.24
 */
MMNetworkError
mm_network_rejection_get_error (MMNetworkRejection *self)
{
    g_return_val_if_fail (MM_IS_NETWORK_REJECTION (self), 0);

    return self->priv->error;
}

/**
 * mm_network_rejection_set_error: (skip)
 */
void
mm_network_rejection_set_error (MMNetworkRejection *self,
                                MMNetworkError      error)
{
    g_return_if_fail (MM_IS_NETWORK_REJECTION (self));

    self->priv->error = error;
}

/*****************************************************************************/

/**
 * mm_network_rejection_get_access_technology:
 * @self: a #MMNetworkRejection.
 *
 * Gets the available class reported with network reject.
 *
 * Returns: the available class.
 *
 * Since: 1.24
 */
MMModemAccessTechnology
mm_network_rejection_get_access_technology (MMNetworkRejection *self)
{
    g_return_val_if_fail (MM_IS_NETWORK_REJECTION (self), 0);

    return self->priv->access_technology;
}

/**
 * mm_network_rejection_set_access_technology: (skip)
 */
void
mm_network_rejection_set_access_technology (MMNetworkRejection     *self,
                                            MMModemAccessTechnology access_technology)
{
    g_return_if_fail (MM_IS_NETWORK_REJECTION (self));

    self->priv->access_technology = access_technology;
}

/*****************************************************************************/


/**
 * mm_network_rejection_get_dictionary: (skip)
 */
GVariant *
mm_network_rejection_get_dictionary (MMNetworkRejection *self)
{
    GVariantBuilder builder;

    if (!self)
        return NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_ERROR,
                           g_variant_new_uint32 (self->priv->error));

    if (self->priv->operator_id)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_OPERATOR_ID,
                               g_variant_new_string (self->priv->operator_id));

    if (self->priv->operator_name)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_OPERATOR_NAME,
                               g_variant_new_string (self->priv->operator_name));

    if (self->priv->access_technology)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ACCESS_TECHNOLOGY,
                               g_variant_new_uint32 (self->priv->access_technology));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

/**
 * mm_network_rejection_new_from_dictionary: (skip)
 */
MMNetworkRejection *
mm_network_rejection_new_from_dictionary (GVariant *dictionary,
                                          GError   **error)
{
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMNetworkRejection *self;

    self = mm_network_rejection_new ();
    if (!dictionary)
        return self;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create network rejection from dictionary: "
                     "invalid variant type received");
        g_object_unref (self);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        if (g_str_equal (key, PROPERTY_ERROR)) {
            mm_network_rejection_set_error (
                self,
                g_variant_get_uint32 (value));
        } else if (g_str_equal (key, PROPERTY_OPERATOR_ID)) {
            mm_network_rejection_set_operator_id (
                self,
                g_variant_get_string (value, NULL));
        } else if (g_str_equal (key, PROPERTY_OPERATOR_NAME)) {
            mm_network_rejection_set_operator_name (
                self,
                g_variant_get_string (value, NULL));
        } else if (g_str_equal (key, PROPERTY_ACCESS_TECHNOLOGY)) {
            mm_network_rejection_set_access_technology (
                self,
                g_variant_get_uint32 (value));
        }

        g_free (key);
        g_variant_unref (value);
    }

    return self;
}

/*****************************************************************************/

/**
 * mm_network_rejection_new: (skip)
 */
MMNetworkRejection *
mm_network_rejection_new (void)
{
    return (MM_NETWORK_REJECTION (
                g_object_new (MM_TYPE_NETWORK_REJECTION, NULL)));
}

static void
mm_network_rejection_init (MMNetworkRejection *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_NETWORK_REJECTION,
                                              MMNetworkRejectionPrivate);
}

static void
finalize (GObject *object)
{
    MMNetworkRejection *self = MM_NETWORK_REJECTION (object);

    g_free (self->priv->operator_id);
    g_free (self->priv->operator_name);

    G_OBJECT_CLASS (mm_network_rejection_parent_class)->finalize (object);
}

static void
mm_network_rejection_class_init (MMNetworkRejectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMNetworkRejectionPrivate));

    object_class->finalize = finalize;
}
