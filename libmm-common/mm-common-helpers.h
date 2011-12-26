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
gchar *mm_common_get_bands_string (const MMModemBand *bands,
                                   guint n_bands);

GArray   *mm_common_bands_variant_to_garray (GVariant *variant);
GVariant *mm_common_bands_array_to_variant  (const MMModemBand *bands,
                                             guint n_bands);
GVariant *mm_common_bands_garray_to_variant (GArray *array);

#endif /* MM_COMMON_HELPERS_H */
