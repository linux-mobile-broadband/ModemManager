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
 * Copyright (C) 2015 Azimut Electronics
 *
 * Author: Aleksander Morgado <aleksander@aleksander.es>
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-bearer-stats.h"

/**
 * SECTION: mm-bearer-stats
 * @title: MMBearerStats
 * @short_description: Helper object to handle bearer stats.
 *
 * The #MMBearerStats is an object handling the statistics reported by the
 * bearer object during a connection.
 *
 * This object is retrieved with either mm_bearer_get_stats() or
 * mm_bearer_peek_stats().
 */

G_DEFINE_TYPE (MMBearerStats, mm_bearer_stats, G_TYPE_OBJECT)

#define PROPERTY_DURATION "duration"
#define PROPERTY_RX_BYTES "rx-bytes"
#define PROPERTY_TX_BYTES "tx-bytes"

struct _MMBearerStatsPrivate {
    guint   duration;
    guint64 rx_bytes;
    guint64 tx_bytes;
};

/*****************************************************************************/

/**
 * mm_bearer_stats_get_duration:
 * @self: a #MMBearerStats.
 *
 * Gets the duration of the current connection, in seconds.
 *
 * Returns: a #guint.
 */
guint
mm_bearer_stats_get_duration (MMBearerStats *self)
{
    g_return_val_if_fail (MM_IS_BEARER_STATS (self), 0);

    return self->priv->duration;
}

void
mm_bearer_stats_set_duration (MMBearerStats *self,
                              guint duration)
{
    g_return_if_fail (MM_IS_BEARER_STATS (self));

    self->priv->duration = duration;
}

/*****************************************************************************/

/**
 * mm_bearer_stats_get_rx_bytes:
 * @self: a #MMBearerStats.
 *
 * Gets the number of bytes received without error in the connection.
 *
 * Returns: a #guint64.
 */
guint64
mm_bearer_stats_get_rx_bytes (MMBearerStats *self)
{
    g_return_val_if_fail (MM_IS_BEARER_STATS (self), 0);

    return self->priv->rx_bytes;
}

void
mm_bearer_stats_set_rx_bytes (MMBearerStats *self,
                              guint64 bytes)
{
    g_return_if_fail (MM_IS_BEARER_STATS (self));

    self->priv->rx_bytes = bytes;
}

/*****************************************************************************/

/**
 * mm_bearer_stats_get_tx_bytes:
 * @self: a #MMBearerStats.
 *
 * Gets the number of bytes transmitted without error in the connection.
 *
 * Returns: a #guint64.
 */
guint64
mm_bearer_stats_get_tx_bytes (MMBearerStats *self)
{
    g_return_val_if_fail (MM_IS_BEARER_STATS (self), 0);

    return self->priv->tx_bytes;
}

void
mm_bearer_stats_set_tx_bytes (MMBearerStats *self,
                              guint64 bytes)
{
    g_return_if_fail (MM_IS_BEARER_STATS (self));

    self->priv->tx_bytes = bytes;
}

/*****************************************************************************/

GVariant *
mm_bearer_stats_get_dictionary (MMBearerStats *self)
{
    GVariantBuilder builder;

    /* We do allow self==NULL. We'll just report NULL. */
    if (!self)
        return NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add  (&builder,
                            "{sv}",
                            PROPERTY_DURATION,
                            g_variant_new_uint32 (self->priv->duration));
    g_variant_builder_add  (&builder,
                            "{sv}",
                            PROPERTY_RX_BYTES,
                            g_variant_new_uint64 (self->priv->rx_bytes));
    g_variant_builder_add  (&builder,
                            "{sv}",
                            PROPERTY_TX_BYTES,
                            g_variant_new_uint64 (self->priv->tx_bytes));
    return g_variant_builder_end (&builder);
}

/*****************************************************************************/

MMBearerStats *
mm_bearer_stats_new_from_dictionary (GVariant *dictionary,
                                     GError **error)
{
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMBearerStats *self;

    self = mm_bearer_stats_new ();
    if (!dictionary)
        return self;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create Stats from dictionary: "
                     "invalid variant type received");
        g_object_unref (self);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        if (g_str_equal (key, PROPERTY_DURATION)) {
            mm_bearer_stats_set_duration (
                self,
                g_variant_get_uint32 (value));
        } else if (g_str_equal (key, PROPERTY_RX_BYTES)) {
            mm_bearer_stats_set_rx_bytes (
                self,
                g_variant_get_uint64 (value));
        } else if (g_str_equal (key, PROPERTY_TX_BYTES)) {
            mm_bearer_stats_set_tx_bytes (
                self,
                g_variant_get_uint64 (value));
        }
        g_free (key);
        g_variant_unref (value);
    }

    return self;
}

/*****************************************************************************/

MMBearerStats *
mm_bearer_stats_new (void)
{
    return (MM_BEARER_STATS (g_object_new (MM_TYPE_BEARER_STATS, NULL)));
}

static void
mm_bearer_stats_init (MMBearerStats *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_BEARER_STATS, MMBearerStatsPrivate);
}

static void
mm_bearer_stats_class_init (MMBearerStatsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerStatsPrivate));
}
