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
 * Copyright (C) 2010 - 2012 Red Hat, Inc.
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
mm_common_build_capabilities_string (const MMModemCapability *capabilities,
                                     guint n_capabilities)
{
    gboolean first = TRUE;
    GString *str;
    guint i;

    if (!capabilities || !n_capabilities)
        return g_strdup ("none");

    str = g_string_new ("");
    for (i = 0; i < n_capabilities; i++) {
        gchar *tmp;

        tmp = mm_modem_capability_build_string_from_mask (capabilities[i]);
        g_string_append_printf (str, "%s%s",
                                first ? "" : "\n",
                                tmp);
        g_free (tmp);

        if (first)
            first = FALSE;
    }

    return g_string_free (str, FALSE);
}

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
mm_common_build_ports_string (const MMModemPortInfo *ports,
                              guint n_ports)
{
    gboolean first = TRUE;
    GString *str;
    guint i;

    if (!ports || !n_ports)
        return g_strdup ("none");

    str = g_string_new ("");
    for (i = 0; i < n_ports; i++) {
        g_string_append_printf (str, "%s%s (%s)",
                                first ? "" : ", ",
                                ports[i].name,
                                mm_modem_port_type_get_string (ports[i].type));

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

gchar *
mm_common_build_mode_combinations_string (const MMModemModeCombination *modes,
                                          guint n_modes)
{
    gboolean first = TRUE;
    GString *str;
    guint i;

    if (!modes || !n_modes)
        return g_strdup ("none");

    str = g_string_new ("");
    for (i = 0; i < n_modes; i++) {
        gchar *allowed;
        gchar *preferred;

        allowed = mm_modem_mode_build_string_from_mask (modes[i].allowed);
        preferred = mm_modem_mode_build_string_from_mask (modes[i].preferred);
        g_string_append_printf (str, "%sallowed: %s; preferred: %s",
                                first ? "" : "\n",
                                allowed,
                                preferred);
        g_free (allowed);
        g_free (preferred);

        if (first)
            first = FALSE;
    }

    return g_string_free (str, FALSE);
}

GArray *
mm_common_sms_storages_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        GVariantIter iter;
        guint n;

        g_variant_iter_init (&iter, variant);
        n = g_variant_iter_n_children (&iter);

        if (n > 0) {
            guint32 storage;

            array = g_array_sized_new (FALSE, FALSE, sizeof (MMSmsStorage), n);
            while (g_variant_iter_loop (&iter, "u", &storage))
                g_array_append_val (array, storage);
        }
    }

    return array;
}

MMSmsStorage *
mm_common_sms_storages_variant_to_array (GVariant *variant,
                                         guint *n_storages)
{
    GArray *array;

    array = mm_common_sms_storages_variant_to_garray (variant);
    if (n_storages)
        *n_storages = array->len;
    return (MMSmsStorage *) g_array_free (array, FALSE);
}

GVariant *
mm_common_sms_storages_array_to_variant (const MMSmsStorage *storages,
                                         guint n_storages)
{
    GVariantBuilder builder;
    guint i;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));

    for (i = 0; i < n_storages; i++)
        g_variant_builder_add_value (&builder,
                                     g_variant_new_uint32 ((guint32)storages[i]));
    return g_variant_builder_end (&builder);
}

GVariant *
mm_common_sms_storages_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_sms_storages_array_to_variant ((const MMSmsStorage *)array->data,
                                                        array->len);

    return mm_common_sms_storages_array_to_variant (NULL, 0);
}

GArray *
mm_common_ports_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        guint i;
        guint n;

        n = g_variant_n_children (variant);

        if (n > 0) {
            array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemPortInfo), n);
            for (i = 0; i < n; i++) {
                MMModemPortInfo info;

                g_variant_get_child (variant, i, "(su)", &info.name, &info.type);
                g_array_append_val (array, info);
            }
        }
    }

    return array;
}

MMModemPortInfo *
mm_common_ports_variant_to_array (GVariant *variant,
                                  guint *n_ports)
{
    GArray *array;

    array = mm_common_ports_variant_to_garray (variant);
    if (n_ports)
        *n_ports = array->len;
    return (MMModemPortInfo *) g_array_free (array, FALSE);
}

GVariant *
mm_common_ports_array_to_variant (const MMModemPortInfo *ports,
                                  guint n_ports)
{
    GVariantBuilder builder;
    guint i;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(su)"));

    for (i = 0; i < n_ports; i++) {
        GVariant *tuple[2];

        tuple[0] = g_variant_new_string (ports[i].name);
        tuple[1] = g_variant_new_uint32 ((guint32)ports[i].type);
        g_variant_builder_add_value (&builder, g_variant_new_tuple (tuple, 2));
    }
    return g_variant_builder_end (&builder);
}

GVariant *
mm_common_ports_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_ports_array_to_variant ((const MMModemPortInfo *)array->data,
                                                 array->len);

    return mm_common_ports_array_to_variant (NULL, 0);
}

MMModemCapability
mm_common_get_capabilities_from_string (const gchar *str,
                                        GError **error)
{
    GError *inner_error = NULL;
    MMModemCapability capabilities;
    gchar **capability_strings;
    GFlagsClass *flags_class;

    capabilities = MM_MODEM_CAPABILITY_NONE;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_MODEM_CAPABILITY));
    capability_strings = g_strsplit (str, "|", -1);

    if (capability_strings) {
        guint i;

        for (i = 0; capability_strings[i]; i++) {
            guint j;
            gboolean found = FALSE;

            for (j = 0; flags_class->values[j].value_nick; j++) {
                if (!g_ascii_strcasecmp (capability_strings[i], flags_class->values[j].value_nick)) {
                    capabilities |= flags_class->values[j].value;
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                inner_error = g_error_new (
                    MM_CORE_ERROR,
                    MM_CORE_ERROR_INVALID_ARGS,
                    "Couldn't match '%s' with a valid MMModemCapability value",
                    capability_strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        capabilities = MM_MODEM_CAPABILITY_NONE;
    }

    g_type_class_unref (flags_class);
    g_strfreev (capability_strings);
    return capabilities;
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

GArray *
mm_common_capability_combinations_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        GVariantIter iter;
        guint n;

        g_variant_iter_init (&iter, variant);
        n = g_variant_iter_n_children (&iter);

        if (n > 0) {
            guint32 capability;

            array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemCapability), n);
            while (g_variant_iter_loop (&iter, "u", &capability))
                g_array_append_val (array, capability);
        }
    }

    /* If nothing set, fallback to default */
    if (!array) {
        guint32 capability = MM_MODEM_CAPABILITY_NONE;

        array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemCapability), 1);
        g_array_append_val (array, capability);
    }

    return array;
}

MMModemCapability *
mm_common_capability_combinations_variant_to_array (GVariant *variant,
                                                    guint *n_capabilities)
{
    GArray *array;

    array = mm_common_capability_combinations_variant_to_garray (variant);
    if (n_capabilities)
        *n_capabilities = array->len;
    return (MMModemCapability *) g_array_free (array, FALSE);
}

GVariant *
mm_common_capability_combinations_array_to_variant (const MMModemCapability *capabilities,
                                                    guint n_capabilities)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));

    if (n_capabilities > 0) {
        guint i;

        for (i = 0; i < n_capabilities; i++)
            g_variant_builder_add_value (&builder,
                                         g_variant_new_uint32 ((guint32)capabilities[i]));
    } else
        g_variant_builder_add_value (&builder,
                                     g_variant_new_uint32 (MM_MODEM_CAPABILITY_NONE));

    return g_variant_builder_end (&builder);
}

GVariant *
mm_common_capability_combinations_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_capability_combinations_array_to_variant ((const MMModemCapability *)array->data,
                                                                   array->len);

    return mm_common_capability_combinations_array_to_variant (NULL, 0);
}

GVariant *
mm_common_build_capability_combinations_none (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));
    g_variant_builder_add_value (&builder,
                                 g_variant_new_uint32 (MM_MODEM_CAPABILITY_NONE));
    return g_variant_builder_end (&builder);
}

GVariant *
mm_common_build_capability_combinations_any (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));
    g_variant_builder_add_value (&builder,
                                 g_variant_new_uint32 (MM_MODEM_CAPABILITY_ANY));
    return g_variant_builder_end (&builder);
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

void
mm_common_bands_garray_sort (GArray *array)
{
    g_array_sort (array, (GCompareFunc) cmp_band);
}

GArray *
mm_common_mode_combinations_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        GVariantIter iter;
        guint n;

        g_variant_iter_init (&iter, variant);
        n = g_variant_iter_n_children (&iter);

        if (n > 0) {
            MMModemModeCombination mode;

            array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), n);
            while (g_variant_iter_loop (&iter, "(uu)", &mode.allowed, &mode.preferred))
                g_array_append_val (array, mode);
        }
    }

    /* If nothing set, fallback to default */
    if (!array) {
        MMModemModeCombination default_mode;

        default_mode.allowed = MM_MODEM_MODE_ANY;
        default_mode.preferred = MM_MODEM_MODE_NONE;
        array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);
        g_array_append_val (array, default_mode);
    }

    return array;
}

MMModemModeCombination *
mm_common_mode_combinations_variant_to_array (GVariant *variant,
                                              guint *n_modes)
{
    GArray *array;

    array = mm_common_mode_combinations_variant_to_garray (variant);
    if (n_modes)
        *n_modes = array->len;
    return (MMModemModeCombination *) g_array_free (array, FALSE);
}

GVariant *
mm_common_mode_combinations_array_to_variant (const MMModemModeCombination *modes,
                                              guint n_modes)
{
    if (n_modes > 0) {
        GVariantBuilder builder;
        guint i;

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(uu)"));

        for (i = 0; i < n_modes; i++)
            g_variant_builder_add_value (&builder,
                                         g_variant_new ("(uu)",
                                                        ((guint32)modes[i].allowed),
                                                        ((guint32)modes[i].preferred)));
        return g_variant_builder_end (&builder);
    }

    return mm_common_build_mode_combinations_default ();
}

GVariant *
mm_common_mode_combinations_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_mode_combinations_array_to_variant ((const MMModemModeCombination *)array->data,
                                                             array->len);

    return mm_common_mode_combinations_array_to_variant (NULL, 0);
}

GVariant *
mm_common_build_mode_combinations_default (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(uu)"));
    g_variant_builder_add_value (&builder,
                                 g_variant_new ("(uu)",
                                                MM_MODEM_MODE_ANY,
                                                MM_MODEM_MODE_NONE));
    return g_variant_builder_end (&builder);
}

GArray *
mm_common_oma_pending_network_initiated_sessions_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        GVariantIter iter;
        guint n;

        g_variant_iter_init (&iter, variant);
        n = g_variant_iter_n_children (&iter);

        if (n > 0) {
            MMOmaPendingNetworkInitiatedSession session;

            array = g_array_sized_new (FALSE, FALSE, sizeof (MMOmaPendingNetworkInitiatedSession), n);
            while (g_variant_iter_loop (&iter, "(uu)", &session.session_type, &session.session_id))
                g_array_append_val (array, session);
        }
    }

    /* If nothing set, fallback to empty */
    if (!array)
        array = g_array_new (FALSE, FALSE, sizeof (MMOmaPendingNetworkInitiatedSession));

    return array;
}

MMOmaPendingNetworkInitiatedSession *
mm_common_oma_pending_network_initiated_sessions_variant_to_array (GVariant *variant,
                                                                   guint *n_sessions)
{
    GArray *array;

    array = mm_common_oma_pending_network_initiated_sessions_variant_to_garray (variant);
    if (n_sessions)
        *n_sessions = array->len;
    return (MMOmaPendingNetworkInitiatedSession *) g_array_free (array, FALSE);
}

GVariant *
mm_common_oma_pending_network_initiated_sessions_array_to_variant (const MMOmaPendingNetworkInitiatedSession *sessions,
                                                                   guint n_sessions)
{
    if (n_sessions > 0) {
        GVariantBuilder builder;
        guint i;

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(uu)"));

        for (i = 0; i < n_sessions; i++)
            g_variant_builder_add_value (&builder,
                                         g_variant_new ("(uu)",
                                                        ((guint32)sessions[i].session_type),
                                                        ((guint32)sessions[i].session_id)));
        return g_variant_builder_end (&builder);
    }

    return mm_common_build_oma_pending_network_initiated_sessions_default ();
}

GVariant *
mm_common_oma_pending_network_initiated_sessions_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_oma_pending_network_initiated_sessions_array_to_variant ((const MMOmaPendingNetworkInitiatedSession *)array->data,
                                                                                  array->len);

    return mm_common_oma_pending_network_initiated_sessions_array_to_variant (NULL, 0);
}

GVariant *
mm_common_build_oma_pending_network_initiated_sessions_default (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(uu)"));
    return g_variant_builder_end (&builder);
}

gboolean
mm_common_get_boolean_from_string (const gchar *value,
                                   GError **error)
{
    if (!g_ascii_strcasecmp (value, "true") || g_str_equal (value, "1"))
        return TRUE;

    if (!g_ascii_strcasecmp (value, "false") || g_str_equal (value, "0"))
        return FALSE;

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
    GFlagsClass *flags_class;
    guint i;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_BEARER_IP_FAMILY));

    for (i = 0; flags_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, flags_class->values[i].value_nick))
            return flags_class->values[i].value;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid MMBearerIpFamily value",
                 str);
    return MM_BEARER_IP_FAMILY_NONE;
}

MMBearerAllowedAuth
mm_common_get_allowed_auth_from_string (const gchar *str,
                                        GError **error)
{
    GError *inner_error = NULL;
    MMBearerAllowedAuth allowed_auth;
    gchar **strings;
    GFlagsClass *flags_class;

    allowed_auth = MM_BEARER_ALLOWED_AUTH_UNKNOWN;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_BEARER_ALLOWED_AUTH));
    strings = g_strsplit (str, "|", -1);

    if (strings) {
        guint i;

        for (i = 0; strings[i]; i++) {
            guint j;
            gboolean found = FALSE;

            for (j = 0; flags_class->values[j].value_nick; j++) {
                if (!g_ascii_strcasecmp (strings[i], flags_class->values[j].value_nick)) {
                    allowed_auth |= flags_class->values[j].value;
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                inner_error = g_error_new (
                    MM_CORE_ERROR,
                    MM_CORE_ERROR_INVALID_ARGS,
                    "Couldn't match '%s' with a valid MMBearerAllowedAuth value",
                    strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        allowed_auth = MM_BEARER_ALLOWED_AUTH_UNKNOWN;
    }

    g_type_class_unref (flags_class);
    g_strfreev (strings);
    return allowed_auth;
}

MMSmsStorage
mm_common_get_sms_storage_from_string (const gchar *str,
                                       GError **error)
{
    GEnumClass *enum_class;
    guint i;

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_SMS_STORAGE));

    for (i = 0; enum_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, enum_class->values[i].value_nick))
            return enum_class->values[i].value;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid MMSmsStorage value",
                 str);
    return MM_SMS_STORAGE_UNKNOWN;
}

MMSmsCdmaTeleserviceId
mm_common_get_sms_cdma_teleservice_id_from_string (const gchar *str,
                                                   GError **error)
{
    GEnumClass *enum_class;
    guint i;

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_SMS_CDMA_TELESERVICE_ID));

    for (i = 0; enum_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, enum_class->values[i].value_nick))
            return enum_class->values[i].value;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid MMSmsCdmaTeleserviceId value",
                 str);
    return MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN;
}

MMSmsCdmaServiceCategory
mm_common_get_sms_cdma_service_category_from_string (const gchar *str,
                                                     GError **error)
{
    GEnumClass *enum_class;
    guint i;

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_SMS_CDMA_SERVICE_CATEGORY));

    for (i = 0; enum_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, enum_class->values[i].value_nick))
            return enum_class->values[i].value;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid MMSmsCdmaServiceCategory value",
                 str);
    return MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN;
}

MMCallDirection
mm_common_get_call_direction_from_string (const gchar *str,
                                          GError **error)
{
    GEnumClass *enum_class;
    guint i;

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_CALL_DIRECTION));

    for (i = 0; enum_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, enum_class->values[i].value_nick))
            return enum_class->values[i].value;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid MMCallDirection value",
                 str);
    return MM_CALL_DIRECTION_UNKNOWN;
}

MMCallState
mm_common_get_call_state_from_string (const gchar *str,
                                      GError **error)
{
    GEnumClass *enum_class;
    guint i;

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_CALL_STATE));

    for (i = 0; enum_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, enum_class->values[i].value_nick))
            return enum_class->values[i].value;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid MMCallState value",
                 str);
    return MM_CALL_STATE_UNKNOWN;
}

MMCallStateReason
mm_common_get_call_state_reason_from_string (const gchar *str,
                                             GError **error)
{
    GEnumClass *enum_class;
    guint i;

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_CALL_STATE_REASON));

    for (i = 0; enum_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, enum_class->values[i].value_nick))
            return enum_class->values[i].value;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid MMCallStateReason value",
                 str);
    return MM_CALL_STATE_REASON_UNKNOWN;
}

MMOmaFeature
mm_common_get_oma_features_from_string (const gchar *str,
                                        GError **error)
{
    GError *inner_error = NULL;
    MMOmaFeature features;
    gchar **feature_strings;
    GFlagsClass *flags_class;

    features = MM_OMA_FEATURE_NONE;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_OMA_FEATURE));
    feature_strings = g_strsplit (str, "|", -1);

    if (feature_strings) {
        guint i;

        for (i = 0; feature_strings[i]; i++) {
            guint j;
            gboolean found = FALSE;

            for (j = 0; flags_class->values[j].value_nick; j++) {
                if (!g_ascii_strcasecmp (feature_strings[i], flags_class->values[j].value_nick)) {
                    features |= flags_class->values[j].value;
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                inner_error = g_error_new (
                    MM_CORE_ERROR,
                    MM_CORE_ERROR_INVALID_ARGS,
                    "Couldn't match '%s' with a valid MMOmaFeature value",
                    feature_strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        features = MM_OMA_FEATURE_NONE;
    }

    g_type_class_unref (flags_class);
    g_strfreev (feature_strings);
    return features;
}

MMOmaSessionType
mm_common_get_oma_session_type_from_string (const gchar *str,
                                            GError **error)
{
    GEnumClass *enum_class;
    guint i;

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_OMA_SESSION_TYPE));

    for (i = 0; enum_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, enum_class->values[i].value_nick))
            return enum_class->values[i].value;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid MMOmaSessionType value",
                 str);
    return MM_OMA_SESSION_TYPE_UNKNOWN;
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
        if (str[num] != '+' && str[num] != '-' && !g_ascii_isdigit (str[num]))
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
    if (!s)
        return FALSE;

    ret = mm_get_int_from_str (s, out);
    g_free (s);

    return ret;
}

/**
 * mm_get_uint_from_str:
 * @str: the string to convert to an unsigned int
 * @out: on success, the number
 *
 * Converts a string to an unsigned number.  All characters in the string
 * MUST be valid digits (0 - 9), otherwise FALSE is returned.
 *
 * Returns: %TRUE if the string was converted, %FALSE if it was not or if it
 * did not contain only digits.
 */
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

/**
 * mm_get_uint_from_hex_str:
 * @str: the hex string to convert to an unsigned int
 * @out: on success, the number
 *
 * Converts a string to an unsigned number.  All characters in the string
 * MUST be valid hexadecimal digits (0-9, A-F, a-f), otherwise FALSE is
 * returned.
 *
 * An optional "0x" prefix may be given in @str.
 *
 * Returns: %TRUE if the string was converted, %FALSE if it was not or if it
 * did not contain only digits.
 */
gboolean
mm_get_uint_from_hex_str (const gchar *str,
                          guint       *out)
{
    gulong num;

    if (!str)
        return FALSE;

    if (g_str_has_prefix (str, "0x"))
        str = &str[2];

    if (!str[0])
        return FALSE;

    for (num = 0; str[num]; num++) {
        if (!g_ascii_isxdigit (str[num]))
            return FALSE;
    }

    errno = 0;
    num = strtoul (str, NULL, 16);
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
    if (!s)
        return FALSE;

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
    if (!s)
        return FALSE;

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

/*****************************************************************************/

const gchar *
mm_sms_delivery_state_get_string_extended (guint delivery_state)
{
    if (delivery_state > 0x02 && delivery_state < 0x20) {
        if (delivery_state < 0x10)
            return "completed-reason-reserved";
        else
            return "completed-sc-specific-reason";
    }

    if (delivery_state > 0x25 && delivery_state < 0x40) {
        if (delivery_state < 0x30)
            return "temporary-error-reason-reserved";
        else
            return "temporary-error-sc-specific-reason";
    }

    if (delivery_state > 0x49 && delivery_state < 0x60) {
        if (delivery_state < 0x50)
            return "error-reason-reserved";
        else
            return "error-sc-specific-reason";
    }

    if (delivery_state > 0x65 && delivery_state < 0x80) {
        if (delivery_state < 0x70)
            return "temporary-fatal-error-reason-reserved";
        else
            return "temporary-fatal-error-sc-specific-reason";
    }

    if (delivery_state >= 0x80 && delivery_state < 0x100)
        return "unknown-reason-reserved";

    if (delivery_state >= 0x100)
        return "unknown";

    /* Otherwise, use the MMSmsDeliveryState enum as we can match the known
     * value */
    return mm_sms_delivery_state_get_string ((MMSmsDeliveryState)delivery_state);
}

/*****************************************************************************/

/* From hostap, Copyright (c) 2002-2005, Jouni Malinen <jkmaline@cc.hut.fi> */

static gint
hex2num (gchar c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

gint
mm_utils_hex2byte (const gchar *hex)
{
    gint a, b;

    a = hex2num (*hex++);
    if (a < 0)
        return -1;
    b = hex2num (*hex++);
    if (b < 0)
        return -1;
    return (a << 4) | b;
}

gchar *
mm_utils_hexstr2bin (const gchar *hex, gsize *out_len)
{
    const gchar *ipos = hex;
    gchar *buf = NULL;
    gsize i;
    gint a;
    gchar *opos;
    gsize len;

    len = strlen (hex);

    /* Length must be a multiple of 2 */
    g_return_val_if_fail ((len % 2) == 0, NULL);

    opos = buf = g_malloc0 ((len / 2) + 1);
    for (i = 0; i < len; i += 2) {
        a = mm_utils_hex2byte (ipos);
        if (a < 0) {
            g_free (buf);
            return NULL;
        }
        *opos++ = a;
        ipos += 2;
    }
    *out_len = len / 2;
    return buf;
}

/* End from hostap */

gboolean
mm_utils_ishexstr (const gchar *hex)
{
    gsize len;
    gsize i;

    /* Length not multiple of 2? */
    len = strlen (hex);
    if (len % 2 != 0)
        return FALSE;

    for (i = 0; i < len; i++) {
        /* Non-hex char? */
        if (hex[i] >= '0' && hex[i] <= '9')
            continue;
        if (hex[i] >= 'a' && hex[i] <= 'f')
            continue;
        if (hex[i] >= 'A' && hex[i] <= 'F')
            continue;
        return FALSE;
    }

    return TRUE;
}

gchar *
mm_utils_bin2hexstr (const guint8 *bin, gsize len)
{
    GString *ret;
    gsize i;

    g_return_val_if_fail (bin != NULL, NULL);

    ret = g_string_sized_new (len * 2 + 1);
    for (i = 0; i < len; i++)
        g_string_append_printf (ret, "%.2X", bin[i]);
    return g_string_free (ret, FALSE);
}

gboolean
mm_utils_check_for_single_value (guint32 value)
{
    gboolean found = FALSE;
    guint32 i;

    for (i = 1; i <= 32; i++) {
        if (value & 0x1) {
            if (found)
                return FALSE;  /* More than one bit set */
            found = TRUE;
        }
        value >>= 1;
    }

    return TRUE;
}
