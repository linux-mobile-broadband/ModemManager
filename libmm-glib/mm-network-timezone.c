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
#include "mm-network-timezone.h"

/**
 * SECTION: mm-network-timezone
 * @title: MMNetworkTimezone
 * @short_description: Helper object to handle network timezone information.
 *
 * The #MMNetworkTimezone is an object handling the timezone information
 * reported by the network.
 *
 * This object is retrieved with either mm_modem_time_peek_network_timezone()
 * or mm_modem_time_get_network_timezone().
 */

G_DEFINE_TYPE (MMNetworkTimezone, mm_network_timezone, G_TYPE_OBJECT);

struct _MMNetworkTimezonePrivate {
    gint32 offset;
    gint32 dst_offset;
    gint32 leap_seconds;
};

/*****************************************************************************/

/**
 * mm_network_timezone_get_offset:
 * @self: a #MMNetworkTimezone.
 *
 * Gets the timezone offset (in minutes) reported by the network.
 *
 * Returns: the offset, or %MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN if unknown.
 *
 * Since: 1.0
 */
gint
mm_network_timezone_get_offset (MMNetworkTimezone *self)
{
    g_return_val_if_fail (MM_IS_NETWORK_TIMEZONE (self),
                          MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN);

    return self->priv->offset;
}

/**
 * mm_network_timezone_set_offset: (skip)
 */
void
mm_network_timezone_set_offset (MMNetworkTimezone *self,
                                gint offset)
{
    g_return_if_fail (MM_IS_NETWORK_TIMEZONE (self));

    self->priv->offset = offset;
}

/*****************************************************************************/

/**
 * mm_network_timezone_get_dst_offset:
 * @self: a #MMNetworkTimezone.
 *
 * Gets the timezone offset due to daylight saving time (in minutes) reported by
 * the network.
 *
 * Returns: the offset, or %MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN if unknown.
 *
 * Since: 1.0
 */
gint
mm_network_timezone_get_dst_offset (MMNetworkTimezone *self)
{
    g_return_val_if_fail (MM_IS_NETWORK_TIMEZONE (self),
                          MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN);

    return self->priv->dst_offset;
}

/**
 * mm_network_timezone_set_dst_offset: (skip)
 */
void
mm_network_timezone_set_dst_offset (MMNetworkTimezone *self,
                                    gint dst_offset)
{
    g_return_if_fail (MM_IS_NETWORK_TIMEZONE (self));

    self->priv->dst_offset = dst_offset;
}

/*****************************************************************************/

/**
 * mm_network_timezone_get_leap_seconds:
 * @self: a #MMNetworkTimezone.
 *
 * Gets the number of leap seconds (TAI-UTC), as reported by the network.
 *
 * Returns: the number of leap seconds, or
 * %MM_NETWORK_TIMEZONE_LEAP_SECONDS_UNKNOWN if unknown.
 *
 * Since: 1.0
 */
gint
mm_network_timezone_get_leap_seconds (MMNetworkTimezone *self)
{
    g_return_val_if_fail (MM_IS_NETWORK_TIMEZONE (self),
                          MM_NETWORK_TIMEZONE_LEAP_SECONDS_UNKNOWN);

    return self->priv->leap_seconds;
}

/**
 * mm_network_timezone_set_leap_seconds: (skip)
 */
void
mm_network_timezone_set_leap_seconds (MMNetworkTimezone *self,
                                      gint leap_seconds)
{
    g_return_if_fail (MM_IS_NETWORK_TIMEZONE (self));

    self->priv->leap_seconds = leap_seconds;
}

/*****************************************************************************/

/**
 * mm_network_timezone_get_dictionary: (skip)
 */
GVariant *
mm_network_timezone_get_dictionary (MMNetworkTimezone *self)
{
    GVariantBuilder builder;

    /* Allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_NETWORK_TIMEZONE (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->offset != MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN)
        g_variant_builder_add (&builder,
                               "{sv}",
                               "offset",
                               g_variant_new_int32 (self->priv->offset));

    if (self->priv->dst_offset != MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN)
        g_variant_builder_add (&builder,
                               "{sv}",
                               "dst-offset",
                               g_variant_new_int32 (self->priv->dst_offset));

    if (self->priv->leap_seconds != MM_NETWORK_TIMEZONE_LEAP_SECONDS_UNKNOWN)
        g_variant_builder_add (&builder,
                               "{sv}",
                               "leap-seconds",
                               g_variant_new_int32 (self->priv->leap_seconds));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

/**
 * mm_network_timezone_new_from_dictionary: (skip)
 */
MMNetworkTimezone *
mm_network_timezone_new_from_dictionary (GVariant *dictionary,
                                         GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMNetworkTimezone *self;

    self = mm_network_timezone_new ();
    if (!dictionary)
        return self;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create Network Timezone from dictionary: "
                     "invalid variant type received");
        g_object_unref (self);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        /* All currently supported properties are signed integers,
         * so we just check the value type here */
        if (!g_variant_is_of_type (value, G_VARIANT_TYPE_INT32)) {
            /* Set inner error, will stop the loop */
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_INVALID_ARGS,
                                       "Invalid status dictionary, unexpected value type '%s'",
                                       g_variant_get_type_string (value));
        } else if (g_str_equal (key, "offset"))
            self->priv->offset = g_variant_get_int32 (value);
        else if (g_str_equal (key, "dst-offset"))
            self->priv->dst_offset = g_variant_get_int32 (value);
        else if (g_str_equal (key, "leap-seconds"))
            self->priv->leap_seconds = g_variant_get_int32 (value);
        else {
            /* Set inner error, will stop the loop */
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_INVALID_ARGS,
                                       "Invalid status dictionary, unexpected key '%s'",
                                       key);
        }

        g_free (key);
        g_variant_unref (value);
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (self);
        return NULL;
    }

    return self;
}

/*****************************************************************************/

/**
 * mm_network_timezone_new: (skip)
 */
MMNetworkTimezone *
mm_network_timezone_new (void)
{
    return (MM_NETWORK_TIMEZONE (
                g_object_new (MM_TYPE_NETWORK_TIMEZONE, NULL)));
}

static void
mm_network_timezone_init (MMNetworkTimezone *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_NETWORK_TIMEZONE,
                                              MMNetworkTimezonePrivate);

    /* Some defaults */
    self->priv->offset = MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN;
    self->priv->dst_offset = MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN;
    self->priv->leap_seconds = MM_NETWORK_TIMEZONE_LEAP_SECONDS_UNKNOWN;
}

static void
mm_network_timezone_class_init (MMNetworkTimezoneClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMNetworkTimezonePrivate));
}
