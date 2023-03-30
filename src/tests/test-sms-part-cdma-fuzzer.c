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
 * Copyright (C) 2023 Google, Inc.
 */

#include <config.h>
#include <string.h>
#include <stdint.h>
#include <glib.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-sms-part-cdma.h"

int
LLVMFuzzerTestOneInput (const uint8_t *data,
                        size_t         size)
{
    g_autoptr(MMSmsPart) part = NULL;
    g_autoptr(GError)    error = NULL;

    if (!size)
        return 0;

    part = mm_sms_part_cdma_new_from_binary_pdu (0, data, size, NULL, &error);
    g_assert (part || error);
    return 0;
}
