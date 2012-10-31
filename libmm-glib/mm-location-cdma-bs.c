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
 * Copyright (C) 2012 Lanedo GmbH <aleksander@lanedo.com>
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "mm-common-helpers.h"
#include "mm-errors-types.h"
#include "mm-location-cdma-bs.h"

/**
 * SECTION: mm-location-cdma-bs
 * @title: MMLocationCdmaBs
 * @short_description: Helper object to handle CDMA Base Station location information.
 *
 * The #MMLocationCdmaBs is an object handling the location information of the
 * CDMA base station in which the modem is registered.
 *
 * This object is retrieved with either mm_modem_location_get_cdma_bs(),
 * mm_modem_location_get_cdma_bs_sync(), mm_modem_location_get_full() or
 * mm_modem_location_get_full_sync().
 */

G_DEFINE_TYPE (MMLocationCdmaBs, mm_location_cdma_bs, G_TYPE_OBJECT);

#define PROPERTY_LATITUDE  "latitude"
#define PROPERTY_LONGITUDE "longitude"

struct _MMLocationCdmaBsPrivate {
    gdouble  latitude;
    gdouble  longitude;
};

/*****************************************************************************/

/**
 * mm_location_cdma_bs_get_longitude:
 * @self: a #MMLocationCdmaBs.
 *
 * Gets the longitude, in the [-180,180] range.
 *
 * Returns: the longitude, or %MM_LOCATION_LONGITUDE_UNKNOWN if unknown.
 */
gdouble
mm_location_cdma_bs_get_longitude (MMLocationCdmaBs *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_CDMA_BS (self),
                          MM_LOCATION_LONGITUDE_UNKNOWN);

    return self->priv->longitude;
}

/*****************************************************************************/

/**
 * mm_location_cdma_bs_get_latitude:
 * @self: a #MMLocationCdmaBs.
 *
 * Gets the latitude, in the [-90,90] range.
 *
 * Returns: the latitude, or %MM_LOCATION_LATITUDE_UNKNOWN if unknown.
 */
gdouble
mm_location_cdma_bs_get_latitude (MMLocationCdmaBs *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_CDMA_BS (self),
                          MM_LOCATION_LATITUDE_UNKNOWN);

    return self->priv->latitude;
}

/*****************************************************************************/

gboolean
mm_location_cdma_bs_set (MMLocationCdmaBs *self,
                         gdouble longitude,
                         gdouble latitude)
{
    g_return_val_if_fail ((longitude == MM_LOCATION_LONGITUDE_UNKNOWN ||
                           (longitude >= -180.0 && longitude <= 180.0)),
                          FALSE);
    g_return_val_if_fail ((latitude == MM_LOCATION_LATITUDE_UNKNOWN ||
                           (latitude >= -90.0 && latitude <= 90.0)),
                          FALSE);

    if (self->priv->longitude == longitude &&
        self->priv->latitude == latitude)
        return FALSE;

    self->priv->longitude = longitude;
    self->priv->latitude = latitude;
    return TRUE;
}

/*****************************************************************************/

GVariant *
mm_location_cdma_bs_get_dictionary (MMLocationCdmaBs *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_LOCATION_CDMA_BS (self), NULL);

    /* If mandatory parameters are not found, return NULL */
    if (self->priv->longitude == MM_LOCATION_LONGITUDE_UNKNOWN ||
        self->priv->latitude == MM_LOCATION_LATITUDE_UNKNOWN)
        return NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_LONGITUDE,
                           g_variant_new_double (self->priv->longitude));
    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_LATITUDE,
                           g_variant_new_double (self->priv->latitude));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

MMLocationCdmaBs *
mm_location_cdma_bs_new_from_dictionary (GVariant *dictionary,
                                         GError **error)
{
    GError *inner_error = NULL;
    MMLocationCdmaBs *self;
    GVariantIter iter;
    gchar *key;
    GVariant *value;

    self = mm_location_cdma_bs_new ();
    if (!dictionary)
        return self;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create CDMA BS location from dictionary: "
                     "invalid variant type received");
        g_object_unref (self);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        if (g_str_equal (key, PROPERTY_LONGITUDE))
            self->priv->longitude = g_variant_get_double (value);
        else if (g_str_equal (key, PROPERTY_LATITUDE))
            self->priv->latitude = g_variant_get_double (value);
        g_free (key);
        g_variant_unref (value);
    }

    /* If any of the mandatory parameters is missing, cleanup */
    if (self->priv->longitude == MM_LOCATION_LONGITUDE_UNKNOWN ||
        self->priv->latitude == MM_LOCATION_LATITUDE_UNKNOWN) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create CDMA BS location from dictionary: "
                     "mandatory parameters missing "
                     "(longitude: %s, latitude: %s)",
                     (self->priv->longitude != MM_LOCATION_LONGITUDE_UNKNOWN) ? "yes" : "missing",
                     (self->priv->latitude != MM_LOCATION_LATITUDE_UNKNOWN) ? "yes" : "missing");
        g_clear_object (&self);
    }

    return self;
}

/*****************************************************************************/

MMLocationCdmaBs *
mm_location_cdma_bs_new (void)
{
    return (MM_LOCATION_CDMA_BS (
                g_object_new (MM_TYPE_LOCATION_CDMA_BS, NULL)));
}

static void
mm_location_cdma_bs_init (MMLocationCdmaBs *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_LOCATION_CDMA_BS,
                                              MMLocationCdmaBsPrivate);

    self->priv->latitude = MM_LOCATION_LATITUDE_UNKNOWN;
    self->priv->longitude = MM_LOCATION_LONGITUDE_UNKNOWN;
}

static void
mm_location_cdma_bs_class_init (MMLocationCdmaBsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMLocationCdmaBsPrivate));
}
