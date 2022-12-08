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

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <glib.h>
#include <libmm-glib.h>

void mm_test_helpers_compare_bands (GArray            *bands,
                                    const MMModemBand *expected_bands,
                                    guint              n_expected_bands);

#endif /* TEST_HELPERS_H */
