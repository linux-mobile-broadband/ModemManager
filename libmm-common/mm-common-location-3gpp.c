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

#include "mm-enums-types.h"
#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-common-location-3gpp.h"

G_DEFINE_TYPE (MMCommonLocation3gpp, mm_common_location_3gpp, G_TYPE_OBJECT);

struct _MMCommonLocation3gppPrivate {
    guint mobile_country_code;
    guint mobile_network_code;
    gulong location_area_code;
    gulong cell_id;
};

/*****************************************************************************/

guint
mm_common_location_3gpp_get_mobile_country_code (MMCommonLocation3gpp *self)
{
    return self->priv->mobile_country_code;
}

guint
mm_common_location_3gpp_get_mobile_network_code (MMCommonLocation3gpp *self)
{
    return self->priv->mobile_network_code;
}

gulong
mm_common_location_3gpp_get_location_area_code (MMCommonLocation3gpp *self)
{
    return self->priv->location_area_code;
}

gulong
mm_common_location_3gpp_get_cell_id (MMCommonLocation3gpp *self)
{
    return self->priv->cell_id;
}

/* void */
/* mm_common_location_3gpp_clear (MMCommonLocation3gpp *self) */
/* { */
/*     self->priv->mobile_country_code = 0; */
/*     self->priv->mobile_network_code = 0; */
/*     self->priv->location_area_code = 0; */
/*     self->priv->cell_id = 0; */
/* } */

gboolean
mm_common_location_3gpp_set_mobile_country_code (MMCommonLocation3gpp *self,
                                                 guint mobile_country_code)
{
    /* If no change in the location info, don't do anything */
    if (self->priv->mobile_country_code == mobile_country_code)
        return FALSE;

    self->priv->mobile_country_code = mobile_country_code;
    return TRUE;
}

gboolean
mm_common_location_3gpp_set_mobile_network_code (MMCommonLocation3gpp *self,
                                                 guint mobile_network_code)
{
    /* If no change in the location info, don't do anything */
    if (self->priv->mobile_network_code == mobile_network_code)
        return FALSE;

    self->priv->mobile_network_code = mobile_network_code;
    return TRUE;
}

gboolean
mm_common_location_3gpp_set_location_area_code (MMCommonLocation3gpp *self,
                                                gulong location_area_code)
{
    /* If no change in the location info, don't do anything */
    if (self->priv->location_area_code == location_area_code)
        return FALSE;

    self->priv->location_area_code = location_area_code;
    return TRUE;
}


gboolean
mm_common_location_3gpp_set_cell_id (MMCommonLocation3gpp *self,
                                     gulong cell_id)
{
    /* If no change in the location info, don't do anything */
    if (self->priv->cell_id == cell_id)
        return FALSE;

    self->priv->cell_id = cell_id;
    return TRUE;
}

/*****************************************************************************/

GVariant *
mm_common_location_3gpp_get_string_variant (MMCommonLocation3gpp *self)
{
    GVariant *variant = NULL;

    if (self->priv->mobile_country_code &&
        self->priv->mobile_network_code &&
        self->priv->location_area_code &&
        self->priv->cell_id) {
        gchar *str;

        str = g_strdup_printf ("%u,%u,%lX,%lX",
                               self->priv->mobile_country_code,
                               self->priv->mobile_network_code,
                               self->priv->location_area_code,
                               self->priv->cell_id);

        variant = g_variant_new_string (str);
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

MMCommonLocation3gpp *
mm_common_location_3gpp_new_from_string_variant (GVariant *string,
                                                 GError **error)
{
    MMCommonLocation3gpp *self = NULL;
    gchar **split;

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
        validate_numeric_string_content ("Cell ID", split[3], TRUE, error)) {
        /* Create new location object */
        self = mm_common_location_3gpp_new ();
        self->priv->mobile_country_code = strtol (split[0], NULL, 10);
        self->priv->mobile_network_code = strtol (split[1], NULL, 10);
        self->priv->location_area_code = strtol (split[2], NULL, 16);
        self->priv->cell_id = strtol (split[3], NULL, 16);
    }

    g_strfreev (split);
    return self;
}

/*****************************************************************************/

MMCommonLocation3gpp *
mm_common_location_3gpp_new (void)
{
    return (MM_COMMON_LOCATION_3GPP (
                g_object_new (MM_TYPE_COMMON_LOCATION_3GPP, NULL)));
}

static void
mm_common_location_3gpp_init (MMCommonLocation3gpp *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_COMMON_LOCATION_3GPP,
                                              MMCommonLocation3gppPrivate);
}

static void
mm_common_location_3gpp_class_init (MMCommonLocation3gppClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCommonLocation3gppPrivate));
}
