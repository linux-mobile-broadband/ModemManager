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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-quectel.h"

gboolean
mm_quectel_parse_ctzu_test_response (const gchar  *response,
                                     gpointer      log_object,
                                     gboolean     *supports_disable,
                                     gboolean     *supports_enable,
                                     gboolean     *supports_enable_update_rtc,
                                     GError      **error)
{
    g_auto(GStrv)     split = NULL;
    g_autoptr(GArray) modes = NULL;
    guint             i;

    /*
     * Response may be:
     *   - +CTZU: (0,1)
     *   - +CTZU: (0,1,3)
     */

#define N_EXPECTED_GROUPS 1

    split = mm_split_string_groups (mm_strip_tag (response, "+CTZU:"));
    if (!split) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't split the +CTZU test response in groups");
        return FALSE;
    }

    if (g_strv_length (split) != N_EXPECTED_GROUPS) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Cannot parse +CTZU test response: invalid number of groups (%u != %u)",
                     g_strv_length (split), N_EXPECTED_GROUPS);
        return FALSE;
    }

    modes = mm_parse_uint_list (split[0], error);
    if (!modes) {
        g_prefix_error (error, "Failed to parse integer list in +CTZU test response: ");
        return FALSE;
    }

    *supports_disable = FALSE;
    *supports_enable = FALSE;
    *supports_enable_update_rtc = FALSE;

    for (i = 0; i < modes->len; i++) {
        guint mode;

        mode = g_array_index (modes, guint, i);
        switch (mode) {
            case 0:
                *supports_disable = TRUE;
                break;
            case 1:
                *supports_enable = TRUE;
                break;
            case 3:
                *supports_enable_update_rtc = TRUE;
                break;
            default:
                mm_obj_dbg (log_object, "unknown +CTZU mode: %u", mode);
                break;
        }
    }

    return TRUE;
}
