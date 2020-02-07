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
#include <ctype.h>
#include <stdlib.h>

#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-location-3gpp.h"

/**
 * SECTION: mm-location-3gpp
 * @title: MMLocation3gpp
 * @short_description: Helper object to handle 3GPP location information.
 *
 * The #MMLocation3gpp is an object handling the location information of the
 * modem when this is reported by the 3GPP network.
 *
 * This object is retrieved with either mm_modem_location_get_3gpp(),
 * mm_modem_location_get_3gpp_sync(), mm_modem_location_get_full() or
 * mm_modem_location_get_full_sync().
 */

G_DEFINE_TYPE (MMLocation3gpp, mm_location_3gpp, G_TYPE_OBJECT);

struct _MMLocation3gppPrivate {
    guint mobile_country_code;
    guint mobile_network_code;
    gulong location_area_code;
    gulong cell_id;
    gulong tracking_area_code;

    /* We use 0 as default MNC when unknown, and that is a bit problematic if
     * the network operator has actually a 0 MNC (e.g. China Mobile, 46000).
     * We need to explicitly track whether MNC is set or not. */
    gboolean mobile_network_code_set;
};

/*****************************************************************************/

/**
 * mm_location_3gpp_get_mobile_country_code:
 * @self: a #MMLocation3gpp.
 *
 * Gets the Mobile Country Code of the 3GPP network.
 *
 * Returns: the MCC, or 0 if unknown.
 *
 * Since: 1.0
 */
guint
mm_location_3gpp_get_mobile_country_code (MMLocation3gpp *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), 0);

    return self->priv->mobile_country_code;
}

/**
 * mm_location_3gpp_set_mobile_country_code: (skip)
 */
gboolean
mm_location_3gpp_set_mobile_country_code (MMLocation3gpp *self,
                                          guint mobile_country_code)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), FALSE);

    /* If no change in the location info, don't do anything */
    if (self->priv->mobile_country_code == mobile_country_code)
        return FALSE;

    self->priv->mobile_country_code = mobile_country_code;
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_location_3gpp_get_mobile_network_code:
 * @self: a #MMLocation3gpp.
 *
 * Gets the Mobile Network Code of the 3GPP network.
 *
 * Note that 0 may actually be a valid MNC. In general, the MNC should be
 * considered valid just if the reported MCC is valid, as MCC should never
 * be 0.
 *
 * Returns: the MNC, or 0 if unknown.
 *
 * Since: 1.0
 */
guint
mm_location_3gpp_get_mobile_network_code (MMLocation3gpp *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), 0);

    return self->priv->mobile_network_code;
}

/**
 * mm_location_3gpp_set_mobile_network_code: (skip)
 */
gboolean
mm_location_3gpp_set_mobile_network_code (MMLocation3gpp *self,
                                          guint mobile_network_code)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), FALSE);

    /* If no change in the location info, don't do anything */
    if (self->priv->mobile_network_code_set && (self->priv->mobile_network_code == mobile_network_code))
        return FALSE;

    self->priv->mobile_network_code_set = TRUE;
    self->priv->mobile_network_code = mobile_network_code;
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_location_3gpp_get_location_area_code:
 * @self: a #MMLocation3gpp.
 *
 * Gets the location area code of the 3GPP network.
 *
 * Returns: the location area code, or 0 if unknown.
 *
 * Since: 1.0
 */
gulong
mm_location_3gpp_get_location_area_code (MMLocation3gpp *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), 0);

    return self->priv->location_area_code;
}

/**
 * mm_location_3gpp_set_location_area_code: (skip)
 */
gboolean
mm_location_3gpp_set_location_area_code (MMLocation3gpp *self,
                                         gulong location_area_code)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), FALSE);

    /* If no change in the location info, don't do anything */
    if (self->priv->location_area_code == location_area_code)
        return FALSE;

    self->priv->location_area_code = location_area_code;
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_location_3gpp_get_cell_id:
 * @self: a #MMLocation3gpp.
 *
 * Gets the cell ID of the 3GPP network.
 *
 * Returns: the cell ID, or 0 if unknown.
 *
 * Since: 1.0
 */
gulong
mm_location_3gpp_get_cell_id (MMLocation3gpp *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), 0);

    return self->priv->cell_id;
}

/**
 * mm_location_3gpp_set_cell_id: (skip)
 */
gboolean
mm_location_3gpp_set_cell_id (MMLocation3gpp *self,
                              gulong cell_id)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), FALSE);

    /* If no change in the location info, don't do anything */
    if (self->priv->cell_id == cell_id)
        return FALSE;

    self->priv->cell_id = cell_id;
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_location_3gpp_get_tracking_area_code:
 * @self: a #MMLocation3gpp.
 *
 * Gets the location area code of the 3GPP network.
 *
 * Returns: the location area code, or 0 if unknown.
 *
 * Since: 1.10
 */
gulong
mm_location_3gpp_get_tracking_area_code (MMLocation3gpp *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), 0);

    return self->priv->tracking_area_code;
}

/**
 * mm_location_3gpp_set_tracking_area_code: (skip)
 */
gboolean
mm_location_3gpp_set_tracking_area_code (MMLocation3gpp *self,
                                         gulong tracking_area_code)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), FALSE);

    /* If no change in the location info, don't do anything */
    if (self->priv->tracking_area_code == tracking_area_code)
        return FALSE;

    self->priv->tracking_area_code = tracking_area_code;
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_location_3gpp_reset: (skip)
 */
gboolean
mm_location_3gpp_reset (MMLocation3gpp *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), FALSE);

    if (self->priv->mobile_country_code == 0 &&
        !self->priv->mobile_network_code_set &&
        self->priv->mobile_network_code == 0 &&
        self->priv->location_area_code == 0 &&
        self->priv->tracking_area_code == 0 &&
        self->priv->cell_id == 0)
        return FALSE;

    self->priv->mobile_country_code = 0;
    self->priv->mobile_network_code_set = FALSE;
    self->priv->mobile_network_code = 0;
    self->priv->location_area_code = 0;
    self->priv->tracking_area_code = 0;
    self->priv->cell_id = 0;
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_location_3gpp_get_string_variant: (skip)
 */
GVariant *
mm_location_3gpp_get_string_variant (MMLocation3gpp *self)
{
    GVariant *variant = NULL;

    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), NULL);

    if (self->priv->mobile_country_code &&
        self->priv->mobile_network_code_set &&  /* MNC 0 is actually valid! */
        (self->priv->location_area_code || self->priv->tracking_area_code) &&
        self->priv->cell_id) {
        gchar *str;

        str = g_strdup_printf ("%u,%u,%lX,%lX,%lX",
                               self->priv->mobile_country_code,
                               self->priv->mobile_network_code,
                               self->priv->location_area_code,
                               self->priv->cell_id,
                               self->priv->tracking_area_code);

        variant = g_variant_ref_sink (g_variant_new_string (str));
        g_free (str);
    }

    return variant;
}

/*****************************************************************************/

static gboolean
validate_string_length (const gchar *display,
                        const gchar *str,
                        guint max_length,
                        GError **error)
{
    /* Avoid empty strings */
    if (!str || !str[0]) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid %s: none given",
                     display);
        return FALSE;
    }

    /* Check max length of the field */
    if (strlen (str) > max_length) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid %s: longer than the maximum expected (%u): '%s'",
                     display,
                     max_length,
                     str);
        return FALSE;
    }

    return TRUE;
}

static gboolean
validate_numeric_string_content (const gchar *display,
                                 const gchar *str,
                                 gboolean hex,
                                 GError **error)
{
    guint i;

    for (i = 0; str[i]; i++) {
        if ((hex && !g_ascii_isxdigit (str[i])) ||
            (!hex && !g_ascii_isdigit (str[i]))) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_INVALID_ARGS,
                         "Invalid %s: unexpected char (%c): '%s'",
                         display,
                         str[i],
                         str);
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * mm_location_3gpp_new_from_string_variant: (skip)
 */
MMLocation3gpp *
mm_location_3gpp_new_from_string_variant (GVariant *string,
                                          GError **error)
{
    MMLocation3gpp *self = NULL;
    gchar **split;

    if (!g_variant_is_of_type (string, G_VARIANT_TYPE_STRING)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create 3GPP location from string: "
                     "invalid variant type received");
        return NULL;
    }

    split = g_strsplit (g_variant_get_string (string, NULL), ",", -1);
    if (!split) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid 3GPP location string: '%s'",
                     g_variant_get_string (string, NULL));
        return NULL;
    }

    /* Validate fields */
    if (validate_string_length ("MCC", split[0], 3, error) &&
        validate_numeric_string_content ("MCC", split[0], FALSE, error) &&
        validate_string_length ("MNC", split[1], 3, error) &&
        validate_numeric_string_content ("MNC", split[1], FALSE, error) &&
        validate_string_length ("Location area code", split[2], 4, error) &&
        validate_numeric_string_content ("Location area code", split[2], TRUE, error) &&
        validate_string_length ("Cell ID", split[3], 8, error) &&
        validate_numeric_string_content ("Cell ID", split[3], TRUE, error) &&
        validate_string_length ("Tracking area code", split[4], 8, error) &&
        validate_numeric_string_content ("Tracking area code", split[4], TRUE, error)) {
        /* Create new location object */
        self = mm_location_3gpp_new ();
        self->priv->mobile_country_code = strtol (split[0], NULL, 10);
        self->priv->mobile_network_code = strtol (split[1], NULL, 10);
        self->priv->location_area_code = strtol (split[2], NULL, 16);
        self->priv->cell_id = strtol (split[3], NULL, 16);
        self->priv->tracking_area_code = strtol (split[4], NULL, 16);
    }

    g_strfreev (split);
    return self;
}

/*****************************************************************************/

/**
 * mm_location_3gpp_new: (skip)
 */
MMLocation3gpp *
mm_location_3gpp_new (void)
{
    return (MM_LOCATION_3GPP (
                g_object_new (MM_TYPE_LOCATION_3GPP, NULL)));
}

static void
mm_location_3gpp_init (MMLocation3gpp *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_LOCATION_3GPP,
                                              MMLocation3gppPrivate);
}

static void
mm_location_3gpp_class_init (MMLocation3gppClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMLocation3gppPrivate));
}
