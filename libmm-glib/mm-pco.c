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
 * Copyright 2018 Google LLC.
 */

#include <string.h>
#include <glib.h>

#include "mm-enums-types.h"
#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-pco.h"

/**
 * SECTION: mm-pco
 * @title: MMPco
 * @short_description: Helper object to handle 3GPP PCO.
 *
 * The #MMPco is an object handling the raw 3GPP Protocol Configuration Options
 * (PCO) that the modem has received from the network.
 *
 * This object is retrieved with mm_modem_3gpp_get_pco().
 */

G_DEFINE_TYPE (MMPco, mm_pco, G_TYPE_OBJECT)

struct _MMPcoPrivate {
    /* Session ID, signature 'u' */
    guint32 session_id;
    /* Flag indicating if the PCO data is complete or partial, signature 'b' */
    gboolean is_complete;
    /* Raw PCO data, signature 'ay' */
    GBytes *data;
};

/*****************************************************************************/

static GBytes *
_g_variant_get_bytes (GVariant *variant)
{
    GByteArray *byte_array;
    guint num_bytes;
    GVariantIter iter;
    guint8 byte;

    g_assert (g_variant_is_of_type (variant, G_VARIANT_TYPE ("ay")));

    num_bytes = g_variant_n_children (variant);
    if (num_bytes == 0)
        return NULL;

    byte_array = g_byte_array_sized_new (num_bytes);

    g_variant_iter_init (&iter, variant);
    while (g_variant_iter_loop (&iter, "y", &byte))
        g_byte_array_append (byte_array, &byte, sizeof (byte));

    return g_byte_array_free_to_bytes (byte_array);
}

/*****************************************************************************/

/**
 * mm_pco_get_session_id:
 * @self: a #MMPco.
 *
 * Gets the session ID associated with the PCO.
 *
 * Returns: the session ID.
 *
 * Since: 1.10
 */
guint32
mm_pco_get_session_id (MMPco *self)
{
    g_return_val_if_fail (MM_IS_PCO (self), G_MAXUINT32);

    return self->priv->session_id;
}

/**
 * mm_pco_set_session_id: (skip)
 */
void
mm_pco_set_session_id (MMPco *self,
                       guint32 session_id)
{
    g_return_if_fail (MM_IS_PCO (self));

    self->priv->session_id = session_id;
}

/*****************************************************************************/

/**
 * mm_pco_is_complete:
 * @self: a #MMPco.
 *
 * Gets the complete flag that indicates whether the PCO data contains the
 * complete PCO structure received from the network.
 *
 * Returns: %TRUE if the PCO data contains the complete PCO structure, %FALSE
 * otherwise.
 *
 * Since: 1.10
 */
gboolean
mm_pco_is_complete (MMPco *self)
{
    g_return_val_if_fail (MM_IS_PCO (self), FALSE);

    return self->priv->is_complete;
}

/**
 * mm_pco_set_complete: (skip)
 */
void
mm_pco_set_complete (MMPco *self,
                     gboolean is_complete)
{
    g_return_if_fail (MM_IS_PCO (self));

    self->priv->is_complete = is_complete;
}

/*****************************************************************************/

/**
 * mm_pco_get_data:
 * @self: a #MMPco.
 * @data_size: (out): Size of the PCO data, if any given.
 *
 * Gets the PCO data in raw bytes.
 *
 * Returns: (transfer none): the PCO data, or %NULL if it doesn't contain any.
 *
 * Since: 1.10
 */
const guint8 *
mm_pco_get_data (MMPco *self,
                 gsize *data_size)
{
    g_return_val_if_fail (MM_IS_PCO (self), NULL);

    return g_bytes_get_data (self->priv->data, data_size);
}

/**
 * mm_pco_set_data: (skip)
 */
void
mm_pco_set_data (MMPco *self,
                 const guint8 *data,
                 gsize data_size)
{
    g_return_if_fail (MM_IS_PCO (self));

    g_bytes_unref (self->priv->data);

    self->priv->data = (data && data_size) ? g_bytes_new (data, data_size)
                                           : NULL;
}

/*****************************************************************************/

/**
 * mm_pco_from_variant: (skip)
 */
MMPco *
mm_pco_from_variant (GVariant *variant,
                     GError **error)
{
    MMPco *pco;
    GVariant *pco_data = NULL;

    pco = mm_pco_new ();
    if (!variant)
        return pco;

    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE ("(ubay)"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create PCO from variant: "
                     "invalid variant type received");
        g_object_unref (pco);
        return NULL;
    }

    g_variant_get (variant, "(ub@ay)",
                   &pco->priv->session_id,
                   &pco->priv->is_complete,
                   &pco_data);

    g_bytes_unref (pco->priv->data);
    pco->priv->data = _g_variant_get_bytes (pco_data);
    g_variant_unref (pco_data);

    return pco;
}

/*****************************************************************************/

/**
 * mm_pco_to_variant: (skip)
 */
GVariant *
mm_pco_to_variant (MMPco *self)
{
    GVariantBuilder builder;
    gsize i, pco_data_size;
    const guint8 *pco_data;

    /* Allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_PCO (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("(ubay)"));

    g_variant_builder_add (&builder, "u", self->priv->session_id);
    g_variant_builder_add (&builder, "b", self->priv->is_complete);

    g_variant_builder_open (&builder, G_VARIANT_TYPE ("ay"));
    if (self->priv->data) {
        pco_data = g_bytes_get_data (self->priv->data, &pco_data_size);
        for (i = 0; i < pco_data_size; ++i)
            g_variant_builder_add (&builder, "y", pco_data[i]);
    }
    g_variant_builder_close (&builder);

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

#ifndef MM_DISABLE_DEPRECATED

/**
 * mm_pco_list_free:
 * @pco_list: (transfer full)(element-type ModemManager.Pco): a #GList of
 *  #MMPco.
 *
 * Frees all of the memory used by a #GList of #MMPco.
 *
 * Since: 1.10
 * Deprecated: 1.12.0: Use g_list_free_full() using g_object_unref() as
 * #GDestroyNotify function instead.
 */
void
mm_pco_list_free (GList *pco_list)
{
    g_list_free_full (pco_list, g_object_unref);
}

#endif /* MM_DISABLE_DEPRECATED */

/**
 * mm_pco_list_add: (skip)
 */
GList *
mm_pco_list_add (GList *pco_list,
                 MMPco *pco)
{
    GList *iter;
    guint32 session_id;

    g_return_val_if_fail (pco != NULL, pco_list);

    session_id = mm_pco_get_session_id (pco);

    for (iter = g_list_first (pco_list); iter; iter = g_list_next (iter)) {
        MMPco *iter_pco = iter->data;
        guint32 iter_session_id = mm_pco_get_session_id (iter_pco);

        if (iter_session_id < session_id)
            continue;
        else if (iter_session_id == session_id) {
            iter->data = g_object_ref (pco);
            g_object_unref (iter_pco);
            return pco_list;
        } else
            break;
    }

    return g_list_insert_before (pco_list, iter, g_object_ref (pco));
}

/*****************************************************************************/

/**
 * mm_pco_new: (skip)
 */
MMPco *
mm_pco_new (void)
{
    return (MM_PCO (g_object_new (MM_TYPE_PCO, NULL)));
}

static void
mm_pco_init (MMPco *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_PCO, MMPcoPrivate);

    self->priv->session_id = G_MAXUINT32;
}

static void
finalize (GObject *object)
{
    MMPco *self = MM_PCO (object);

    g_bytes_unref (self->priv->data);

    G_OBJECT_CLASS (mm_pco_parent_class)->finalize (object);
}

static void
mm_pco_class_init (MMPcoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPcoPrivate));

    object_class->finalize = finalize;
}
