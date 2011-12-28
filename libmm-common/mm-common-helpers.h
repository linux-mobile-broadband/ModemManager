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

#include <glib.h>
#include <ModemManager-enums.h>

#ifndef MM_COMMON_HELPERS_H
#define MM_COMMON_HELPERS_H

gchar *mm_common_get_capabilities_string (MMModemCapability caps);
gchar *mm_common_get_access_technologies_string (MMModemAccessTechnology access_tech);
gchar *mm_common_get_modes_string (MMModemMode mode);
gchar *mm_common_get_bands_string (const MMModemBand *bands,
                                   guint n_bands);

MMModemMode mm_common_get_modes_from_string (const gchar *str,
                                             GError **error);
void        mm_common_get_bands_from_string (const gchar *str,
                                             MMModemBand **bands,
                                             guint *n_bands,
                                             GError **error);
gboolean    mm_common_get_boolean_from_string (const gchar *value,
                                               GError **error);

GArray      *mm_common_bands_variant_to_garray (GVariant *variant);
MMModemBand *mm_common_bands_variant_to_array  (GVariant *variant,
                                                guint *n_bands);
GVariant    *mm_common_bands_array_to_variant  (const MMModemBand *bands,
                                                guint n_bands);
GVariant    *mm_common_bands_garray_to_variant (GArray *array);

GVariant    *mm_common_build_bands_any     (void);
GVariant    *mm_common_build_bands_unknown (void);

#endif /* MM_COMMON_HELPERS_H */
