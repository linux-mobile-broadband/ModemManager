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
 * Copyright (C) 2011 - Google, Inc.
 */

#include <gio/gio.h>

#include <ModemManager.h>

#include "mm-enums-types.h"
#include "mm-errors-types.h"
#include "mm-common-helpers.h"

gchar *
mm_common_build_bands_string (const MMModemBand *bands,
                              guint n_bands)
{
    gboolean first = TRUE;
    GString *str;
    guint i;

    if (!bands || !n_bands)
        return g_strdup ("none");

    str = g_string_new ("");
    for (i = 0; i < n_bands; i++) {
        g_string_append_printf (str, "%s%s",
                                first ? "" : ", ",
                                mm_modem_band_get_string (bands[i]));

        if (first)
            first = FALSE;
    }

    return g_string_free (str, FALSE);
}

gchar *
mm_common_build_sms_storages_string (const MMSmsStorage *storages,
                                     guint n_storages)
{
    gboolean first = TRUE;
    GString *str;
    guint i;

    if (!storages || !n_storages)
        return g_strdup ("none");

    str = g_string_new ("");
    for (i = 0; i < n_storages; i++) {
        g_string_append_printf (str, "%s%s",
                                first ? "" : ", ",
                                mm_sms_storage_get_string (storages[i]));

        if (first)
            first = FALSE;
    }

    return g_string_free (str, FALSE);
}

MMModemMode
mm_common_get_modes_from_string (const gchar *str,
                                 GError **error)
{
    GError *inner_error = NULL;
    MMModemMode modes;
    gchar **mode_strings;
	GFlagsClass *flags_class;

    modes = MM_MODEM_MODE_NONE;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_MODEM_MODE));
    mode_strings = g_strsplit (str, "|", -1);

    if (mode_strings) {
        guint i;

        for (i = 0; mode_strings[i]; i++) {
            guint j;
            gboolean found = FALSE;

            for (j = 0; flags_class->values[j].value_nick; j++) {
                if (!g_ascii_strcasecmp (mode_strings[i], flags_class->values[j].value_nick)) {
                    modes |= flags_class->values[j].value;
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                inner_error = g_error_new (
                    MM_CORE_ERROR,
                    MM_CORE_ERROR_INVALID_ARGS,
                    "Couldn't match '%s' with a valid MMModemMode value",
                    mode_strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        modes = MM_MODEM_MODE_NONE;
    }

    g_type_class_unref (flags_class);
    g_strfreev (mode_strings);
    return modes;
}

void
mm_common_get_bands_from_string (const gchar *str,
                                 MMModemBand **bands,
                                 guint *n_bands,
                                 GError **error)
{
    GError *inner_error = NULL;
    GArray *array;
    gchar **band_strings;
	GEnumClass *enum_class;

    array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_BAND));
    band_strings = g_strsplit (str, "|", -1);

    if (band_strings) {
        guint i;

        for (i = 0; band_strings[i]; i++) {
            guint j;
            gboolean found = FALSE;

            for (j = 0; enum_class->values[j].value_nick; j++) {
                if (!g_ascii_strcasecmp (band_strings[i], enum_class->values[j].value_nick)) {
                    g_array_append_val (array, enum_class->values[j].value);
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_INVALID_ARGS,
                                           "Couldn't match '%s' with a valid MMModemBand value",
                                           band_strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_array_free (array, TRUE);
        *n_bands = 0;
        *bands = NULL;
    } else {
        if (!array->len) {
            GEnumValue *value;

            value = g_enum_get_value (enum_class, MM_MODEM_BAND_UNKNOWN);
            g_array_append_val (array, value->value);
        }

        *n_bands = array->len;
        *bands = (MMModemBand *)g_array_free (array, FALSE);
    }

    g_type_class_unref (enum_class);
    g_strfreev (band_strings);
}

GArray *
mm_common_bands_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        GVariantIter iter;
        guint n;

        g_variant_iter_init (&iter, variant);
        n = g_variant_iter_n_children (&iter);

        if (n > 0) {
            guint32 band;

            array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), n);
            while (g_variant_iter_loop (&iter, "u", &band))
                g_array_append_val (array, band);
        }
    }

    /* If nothing set, fallback to default */
    if (!array) {
        guint32 band = MM_MODEM_BAND_UNKNOWN;

        array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
        g_array_append_val (array, band);
    }

    return array;
}

MMModemBand *
mm_common_bands_variant_to_array (GVariant *variant,
                                  guint *n_bands)
{
    GArray *array;

    array = mm_common_bands_variant_to_garray (variant);
    if (n_bands)
        *n_bands = array->len;
    return (MMModemBand *) g_array_free (array, FALSE);
}

GVariant *
mm_common_bands_array_to_variant (const MMModemBand *bands,
                                  guint n_bands)
{
    if (n_bands > 0) {
        GVariantBuilder builder;
        guint i;

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));

        for (i = 0; i < n_bands; i++)
            g_variant_builder_add_value (&builder,
                                         g_variant_new_uint32 ((guint32)bands[i]));
        return g_variant_builder_end (&builder);
    }

    return mm_common_build_bands_unknown ();
}

GVariant *
mm_common_bands_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_bands_array_to_variant ((const MMModemBand *)array->data,
                                                 array->len);

    return mm_common_bands_array_to_variant (NULL, 0);
}

gboolean
mm_common_get_boolean_from_string (const gchar *value,
                                   GError **error)
{
    if (!g_ascii_strcasecmp (value, "true") || g_str_equal (value, "1"))
        return TRUE;

    if (g_ascii_strcasecmp (value, "false") && g_str_equal (value, "0"))
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot get boolean from string '%s'", value);

    return FALSE;
}

MMModemCdmaRmProtocol
mm_common_get_rm_protocol_from_string (const gchar *str,
                                       GError **error)
{
	GEnumClass *enum_class;
    guint i;

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_CDMA_RM_PROTOCOL));


    for (i = 0; enum_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, enum_class->values[i].value_nick))
            return enum_class->values[i].value;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid MMModemCdmaRmProtocol value",
                 str);
    return MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
}

GVariant *
mm_common_build_bands_unknown (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));
    g_variant_builder_add_value (&builder,
                                 g_variant_new_uint32 (MM_MODEM_BAND_UNKNOWN));
    return g_variant_builder_end (&builder);
}

GVariant *
mm_common_build_bands_any (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));
    g_variant_builder_add_value (&builder,
                                 g_variant_new_uint32 (MM_MODEM_BAND_ANY));
    return g_variant_builder_end (&builder);
}
