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
    gchar *operator_code;
    gulong location_area_code;
    gulong cell_id;
    gulong tracking_area_code;
};

/*****************************************************************************/

static gboolean
validate_string_length (const gchar *display,
                        const gchar *str,
                        guint min_length,
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

    /* Check min length of the field */
    if (strlen (str) < min_length) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid %s: shorter than the maximum expected (%u): '%s'",
                     display,
                     min_length,
                     str);
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
    gchar mcc[4];

    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), 0);

    if (!self->priv->operator_code)
        return 0;
    memcpy (mcc, self->priv->operator_code, 3);
    mcc[3] = '\0';
    return strtol (mcc, NULL, 10);
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
 * mm_location_3gpp_get_operator_code:
 * @self: A #MMLocation3gpp.
 *
 * Gets the 3GPP network Mobile Country Code and Mobile Network Code.
 *
 * Returned in the format <literal>"MCCMNC"</literal>, where
 * <literal>MCC</literal> is the three-digit ITU E.212 Mobile Country Code
 * and <literal>MNC</literal> is the two- or three-digit GSM Mobile Network
 * Code. e.g. e<literal>"31026"</literal> or <literal>"310260"</literal>.
 *
 * Returns: (transfer none): The operator code, or %NULL if none available.
 *
 * Since: 1.18
 */
const gchar *
mm_location_3gpp_get_operator_code (MMLocation3gpp *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), NULL);

    return self->priv->operator_code;
}

/**
 * mm_location_3gpp_set_operator_code: (skip)
 */
gboolean
mm_location_3gpp_set_operator_code (MMLocation3gpp *self,
                                    const gchar *operator_code)
{
    g_return_val_if_fail (MM_IS_LOCATION_3GPP (self), FALSE);

    /* If no change in operator code, don't do anything */
    if (!g_strcmp0 (operator_code, self->priv->operator_code))
        return FALSE;

    /* Check the validity here, all other functions expect it's valid. */
    if (operator_code &&
        (!validate_string_length ("MCCMNC", operator_code, 5, 6, NULL) ||
         !validate_numeric_string_content ("MCCMNC", operator_code, FALSE, NULL)))
         return FALSE;

    g_free (self->priv->operator_code);
    self->priv->operator_code = g_strdup (operator_code);
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

    if (self->priv->operator_code == NULL &&
        self->priv->location_area_code == 0 &&
        self->priv->tracking_area_code == 0 &&
        self->priv->cell_id == 0)
        return FALSE;

    g_free (self->priv->operator_code);
    self->priv->operator_code = NULL;
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

    if (self->priv->operator_code &&
        (self->priv->location_area_code || self->priv->tracking_area_code) &&
        self->priv->cell_id) {
        gchar *str;

        str = g_strdup_printf ("%.3s,%s,%lX,%lX,%lX",
                               self->priv->operator_code,
                               self->priv->operator_code + 3,
                               self->priv->location_area_code,
                               self->priv->cell_id,
                               self->priv->tracking_area_code);

        variant = g_variant_ref_sink (g_variant_new_string (str));
        g_free (str);
    }

    return variant;
}

/*****************************************************************************/

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
    if (validate_string_length ("MCC", split[0], 0, 3, error) &&
        validate_numeric_string_content ("MCC", split[0], FALSE, error) &&
        validate_string_length ("MNC", split[1], 0, 3, error) &&
        validate_numeric_string_content ("MNC", split[1], FALSE, error) &&
        validate_string_length ("Location area code", split[2], 0, 4, error) &&
        validate_numeric_string_content ("Location area code", split[2], TRUE, error) &&
        validate_string_length ("Cell ID", split[3], 0, 8, error) &&
        validate_numeric_string_content ("Cell ID", split[3], TRUE, error) &&
        validate_string_length ("Tracking area code", split[4], 0, 8, error) &&
        validate_numeric_string_content ("Tracking area code", split[4], TRUE, error)) {
        /* Create new location object */
        self = mm_location_3gpp_new ();
        /* Join MCC and MNC and ensure they are zero-padded to required widths */
        self->priv->operator_code = g_strdup_printf ("%03lu%0*lu",
                                                     strtoul (split[0], NULL, 10),
                                                     strlen (split[1]) == 3 ? 3 : 2,
                                                     strtoul (split[1], NULL, 10));
        self->priv->location_area_code = strtoul (split[2], NULL, 16);
        self->priv->cell_id = strtoul (split[3], NULL, 16);
        self->priv->tracking_area_code = strtoul (split[4], NULL, 16);
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
finalize (GObject *object)
{
    MMLocation3gpp *self = MM_LOCATION_3GPP (object);

    g_free (self->priv->operator_code);

    G_OBJECT_CLASS (mm_location_3gpp_parent_class)->finalize (object);
}

static void
mm_location_3gpp_class_init (MMLocation3gppClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMLocation3gppPrivate));

    object_class->finalize = finalize;
}
