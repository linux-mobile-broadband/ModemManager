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
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"

#include "test-helpers.h"

void
mm_test_helpers_compare_bands (GArray            *bands,
                               const MMModemBand *expected_bands,
                               guint              n_expected_bands)
{
    gchar  *bands_str;
    GArray *expected_bands_array;
    gchar  *expected_bands_str;

    if (!expected_bands || !n_expected_bands) {
        g_assert (!bands);
        return;
    }

    g_assert (bands);
    mm_common_bands_garray_sort (bands);
    bands_str = mm_common_build_bands_string ((MMModemBand *)(gpointer)(bands->data), bands->len);

    expected_bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), n_expected_bands);
    g_array_append_vals (expected_bands_array, expected_bands, n_expected_bands);
    mm_common_bands_garray_sort (expected_bands_array);
    expected_bands_str = mm_common_build_bands_string ((MMModemBand *)(gpointer)(expected_bands_array->data), expected_bands_array->len);
    g_array_unref (expected_bands_array);

    g_assert_cmpstr (bands_str, ==, expected_bands_str);
    g_free (bands_str);
    g_free (expected_bands_str);
}
