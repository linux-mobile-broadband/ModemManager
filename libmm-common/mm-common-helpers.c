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
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include <string.h>
#include <errno.h>
#include <stdlib.h>
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

static guint
cmp_band (MMModemBand *a, MMModemBand *b)
{
    return (*a - *b);
}

gboolean
mm_common_bands_garray_cmp (GArray *a, GArray *b)
{
    GArray *dup_a;
    GArray *dup_b;
    guint i;
    gboolean different;

    if (a->len != b->len)
        return FALSE;

    dup_a = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), a->len);
    g_array_append_vals (dup_a, a->data, a->len);

    dup_b = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), b->len);
    g_array_append_vals (dup_b, b->data, b->len);

    g_array_sort (dup_a, (GCompareFunc)cmp_band);
    g_array_sort (dup_b, (GCompareFunc)cmp_band);

    different = FALSE;
    for (i = 0; !different && i < a->len; i++) {
        if (g_array_index (dup_a, MMModemBand, i) != g_array_index (dup_b, MMModemBand, i))
            different = TRUE;
    }

    g_array_unref (dup_a);
    g_array_unref (dup_b);

    return !different;
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

MMBearerIpFamily
mm_common_get_ip_type_from_string (const gchar *str,
                                   GError **error)
{
	GEnumClass *enum_class;
    guint i;

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_BEARER_IP_FAMILY));

    for (i = 0; enum_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, enum_class->values[i].value_nick))
            return enum_class->values[i].value;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid MMBearerIpFamily value",
                 str);
    return MM_BEARER_IP_FAMILY_UNKNOWN;
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

/* Expecting input as:
 *   key1=string,key2=true,key3=false...
 * Strings may also be passed enclosed between double or single quotes, like:
 *   key1="this is a string", key2='and so is this'
 */
gboolean
mm_common_parse_key_value_string (const gchar *str,
                                  GError **error,
                                  MMParseKeyValueForeachFn callback,
                                  gpointer user_data)
{
    GError *inner_error = NULL;
    gchar *dup, *p, *key, *key_end, *value, *value_end, quote;

    g_return_val_if_fail (callback != NULL, FALSE);
    g_return_val_if_fail (str != NULL, FALSE);

    /* Allow empty strings, we'll just return with success */
    while (g_ascii_isspace (*str))
        str++;
    if (!str[0])
        return TRUE;

    dup = g_strdup (str);
    p = dup;

    while (TRUE) {
        gboolean keep_iteration = FALSE;

        /* Skip leading spaces */
        while (g_ascii_isspace (*p))
            p++;

        /* Key start */
        key = p;
        if (!g_ascii_isalnum (*key)) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Key must start with alpha/num, starts with '%c'",
                                       *key);
            break;
        }

        /* Key end */
        while (g_ascii_isalnum (*p) || (*p == '-') || (*p == '_'))
            p++;
        key_end = p;
        if (key_end == key) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Couldn't find a proper key");
            break;
        }

        /* Skip whitespaces, if any */
        while (g_ascii_isspace (*p))
            p++;

        /* Equal sign must be here */
        if (*p != '=') {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Couldn't find equal sign separator");
            break;
        }
        /* Skip the equal */
        p++;

        /* Skip whitespaces, if any */
        while (g_ascii_isspace (*p))
            p++;

        /* Do we have a quote-enclosed string? */
        if (*p == '\"' || *p == '\'') {
            quote = *p;
            /* Skip the quote */
            p++;
            /* Value start */
            value = p;
            /* Find the closing quote */
            p = strchr (p, quote);
            if (!p) {
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_FAILED,
                                           "Unmatched quotes in string value");
                break;
            }

            /* Value end */
            value_end = p;
            /* Skip the quote */
            p++;
        } else {
            /* Value start */
            value = p;

            /* Value end */
            while ((*p != ',') && (*p != '\0') && !g_ascii_isspace (*p))
                p++;
            value_end = p;
        }

        /* Note that we allow value == value_end here */

        /* Skip whitespaces, if any */
        while (g_ascii_isspace (*p))
            p++;

        /* If a comma is found, we should keep the iteration */
        if (*p == ',') {
            /* skip the comma */
            p++;
            keep_iteration = TRUE;
        }

        /* Got key and value, prepare them and run the callback */
        *value_end = '\0';
        *key_end = '\0';
        if (!callback (key, value, user_data)) {
            /* We were told to abort */
            break;
        }

        if (keep_iteration)
            continue;

        /* Check if no more key/value pairs expected */
        if (*p == '\0')
            break;

        inner_error = g_error_new (MM_CORE_ERROR,
                                   MM_CORE_ERROR_FAILED,
                                   "Unexpected content (%s) after value",
                                   p);
        break;
    }

    g_free (dup);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/

gboolean
mm_get_int_from_str (const gchar *str,
                     gint *out)
{
    glong num;

    if (!str || !str[0])
        return FALSE;

    for (num = 0; str[num]; num++) {
        if (str[num] != '-' && !g_ascii_isdigit (str[num]))
            return FALSE;
    }

    errno = 0;
    num = strtol (str, NULL, 10);
    if (!errno && num >= G_MININT && num <= G_MAXINT) {
        *out = (gint)num;
        return TRUE;
    }
    return FALSE;
}

gboolean
mm_get_int_from_match_info (GMatchInfo *match_info,
                            guint32 match_index,
                            gint *out)
{
    gchar *s;
    gboolean ret;

    s = g_match_info_fetch (match_info, match_index);
    g_return_val_if_fail (s != NULL, FALSE);

    ret = mm_get_int_from_str (s, out);
    g_free (s);

    return ret;
}

gboolean
mm_get_uint_from_str (const gchar *str,
                      guint *out)
{
    gulong num;

    if (!str || !str[0])
        return FALSE;

    for (num = 0; str[num]; num++) {
        if (!g_ascii_isdigit (str[num]))
            return FALSE;
    }

    errno = 0;
    num = strtoul (str, NULL, 10);
    if (!errno && num <= G_MAXUINT) {
        *out = (guint)num;
        return TRUE;
    }
    return FALSE;
}

gboolean
mm_get_uint_from_match_info (GMatchInfo *match_info,
                             guint32 match_index,
                             guint *out)
{
    gchar *s;
    gboolean ret;

    s = g_match_info_fetch (match_info, match_index);
    g_return_val_if_fail (s != NULL, FALSE);

    ret = mm_get_uint_from_str (s, out);
    g_free (s);

    return ret;
}

gboolean
mm_get_double_from_str (const gchar *str,
                        gdouble *out)
{
    gdouble num;
    guint i;

    if (!str || !str[0])
        return FALSE;

    for (i = 0; str[i]; i++) {
        /* we don't really expect numbers in scientific notation, so
         * don't bother looking for exponents and such */
        if (str[i] != '-' &&
            str[i] != '.' &&
            !g_ascii_isdigit (str[i]))
            return FALSE;
    }

    errno = 0;
    num = strtod (str, NULL);
    if (!errno) {
        *out = num;
        return TRUE;
    }
    return FALSE;
}

gboolean
mm_get_double_from_match_info (GMatchInfo *match_info,
                               guint32 match_index,
                               gdouble *out)
{
    gchar *s;
    gboolean ret;

    s = g_match_info_fetch (match_info, match_index);
    g_return_val_if_fail (s != NULL, FALSE);

    ret = mm_get_double_from_str (s, out);
    g_free (s);

    return ret;
}

gchar *
mm_get_string_unquoted_from_match_info (GMatchInfo *match_info,
                                        guint32 match_index)
{
    gchar *str;
    gsize len;

    str = g_match_info_fetch (match_info, match_index);
    if (!str)
        return NULL;

    len = strlen (str);

    /* Unquote the item if needed */
    if ((len >= 2) && (str[0] == '"') && (str[len - 1] == '"')) {
        str[0] = ' ';
        str[len - 1] = ' ';
        str = g_strstrip (str);
    }

    if (!str[0]) {
        g_free (str);
        return NULL;
    }

    return str;
}
