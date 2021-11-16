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
 * Copyright (C) 2021 UROS Ltd
 */

#include "mm-sim-preferred-network.h"

struct _MMSimPreferredNetwork {
    gchar *operator_code;
    MMModemAccessTechnology access_technology;
};

static MMSimPreferredNetwork *
mm_sim_preferred_network_copy (MMSimPreferredNetwork *preferred_network)
{
    MMSimPreferredNetwork *preferred_network_copy;

    preferred_network_copy = g_slice_new0 (MMSimPreferredNetwork);
    preferred_network_copy->operator_code     = g_strdup (preferred_network->operator_code);
    preferred_network_copy->access_technology = preferred_network->access_technology;

    return preferred_network_copy;
}

G_DEFINE_BOXED_TYPE (MMSimPreferredNetwork, mm_sim_preferred_network, (GBoxedCopyFunc) mm_sim_preferred_network_copy, (GBoxedFreeFunc) mm_sim_preferred_network_free)

/**
 * mm_sim_preferred_network_free:
 * @self: A #MMSimPreferredNetwork.
 *
 * Frees a #MMSimPreferredNetwork.
 *
 * Since: 1.18
 */
void
mm_sim_preferred_network_free (MMSimPreferredNetwork *self)
{
    if (!self)
        return;

    g_free (self->operator_code);
    g_slice_free (MMSimPreferredNetwork, self);
}

/**
 * mm_sim_preferred_network_get_operator_code:
 * @self: A #MMSimPreferredNetwork.
 *
 * Get the operator code (MCCMNC) of the preferred network.
 *
 * Returns: (transfer none): The operator code, or %NULL if none available.
 *
 * Since: 1.18
 */
const gchar *
mm_sim_preferred_network_get_operator_code (const MMSimPreferredNetwork *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->operator_code;
}

/**
 * mm_sim_preferred_network_get_access_technology:
 * @self: A #MMSimPreferredNetwork.
 *
 * Get the access technology mask of the preferred network.
 *
 * Returns: A #MMModemAccessTechnology.
 *
 * Since: 1.18
 */
MMModemAccessTechnology
mm_sim_preferred_network_get_access_technology (const MMSimPreferredNetwork *self)
{
    g_return_val_if_fail (self != NULL, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);

    return self->access_technology;
}

/**
 * mm_sim_preferred_network_set_operator_code:
 * @self: A #MMSimPreferredNetwork.
 * @operator_code: Operator code
 *
 * Set the operator code (MCCMNC) of this preferred network.
 *
 * Since: 1.18
 */
void
mm_sim_preferred_network_set_operator_code (MMSimPreferredNetwork *self,
                                            const gchar *operator_code)
{
    g_return_if_fail (self != NULL);

    g_free (self->operator_code);
    self->operator_code = g_strdup (operator_code);
}

/**
 * mm_sim_preferred_network_set_access_technology:
 * @self: A #MMSimPreferredNetwork.
 * @access_technology: A #MMModemAccessTechnology mask.
 *
 * Set the desired access technologies of this preferred network entry.
 *
 * Since: 1.18
 */
void
mm_sim_preferred_network_set_access_technology (MMSimPreferredNetwork *self,
                                                MMModemAccessTechnology access_technology)
{
    g_return_if_fail (self != NULL);

    self->access_technology = access_technology;
}

/**
 * mm_sim_preferred_network_new:
 *
 * Creates a new empty #MMSimPreferredNetwork.
 *
 * Returns: (transfer full): a #MMSimPreferredNetwork. The returned value should be freed
 * with mm_sim_preferred_network_free().
 *
 * Since: 1.18
 */
MMSimPreferredNetwork *
mm_sim_preferred_network_new (void)
{
    return g_slice_new0 (MMSimPreferredNetwork);
}

/**
 * mm_sim_preferred_network_new_from_variant: (skip)
 */
MMSimPreferredNetwork *
mm_sim_preferred_network_new_from_variant (GVariant *variant)
{
    MMSimPreferredNetwork *preferred_net;

    g_return_val_if_fail (g_variant_is_of_type (variant, G_VARIANT_TYPE ("(su)")),
                          NULL);

    preferred_net = mm_sim_preferred_network_new ();
    g_variant_get (variant, "(su)", &preferred_net->operator_code, &preferred_net->access_technology);
    return preferred_net;
}

/**
 * mm_sim_preferred_network_get_tuple: (skip)
 */
GVariant *
mm_sim_preferred_network_get_tuple (const MMSimPreferredNetwork *self)
{
    return g_variant_new ("(su)",
                          self->operator_code,
                          self->access_technology);
}

/**
 * mm_sim_preferred_network_list_get_variant: (skip)
 */
GVariant *
mm_sim_preferred_network_list_get_variant (const GList *preferred_network_list)
{
    GVariantBuilder  builder;
    const GList     *iter;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(su)"));
    for (iter = preferred_network_list; iter; iter = g_list_next (iter)) {
        g_variant_builder_add_value (&builder,
                                     mm_sim_preferred_network_get_tuple ((const MMSimPreferredNetwork *) iter->data));
    }
    return g_variant_builder_end (&builder);
}

/**
 * mm_sim_preferred_network_list_new_from_variant: (skip)
 */
GList *
mm_sim_preferred_network_list_new_from_variant (GVariant *variant)
{
    GList *network_list = NULL;
    GVariant *child;
    GVariantIter iter;

    g_return_val_if_fail (g_variant_is_of_type (variant, G_VARIANT_TYPE ("a(su)")), NULL);

    g_variant_iter_init (&iter, variant);
    while ((child = g_variant_iter_next_value (&iter))) {
        MMSimPreferredNetwork *preferred_net;

        preferred_net = mm_sim_preferred_network_new_from_variant (child);
        if (preferred_net)
            network_list = g_list_append (network_list, preferred_net);
        g_variant_unref (child);
    }

    return network_list;
}

/**
 * mm_sim_preferred_network_list_copy: (skip)
 */
GList *
mm_sim_preferred_network_list_copy (GList *preferred_network_list)
{
    return g_list_copy_deep (preferred_network_list, (GCopyFunc) mm_sim_preferred_network_copy, NULL);
}

/**
 * mm_sim_preferred_network_list_free: (skip)
 */
void
mm_sim_preferred_network_list_free (GList *preferred_network_list)
{
    g_list_free_full (preferred_network_list, (GDestroyNotify) mm_sim_preferred_network_free);
}
