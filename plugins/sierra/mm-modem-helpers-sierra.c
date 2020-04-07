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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>
#include <string.h>

#include "mm-modem-helpers.h"
#include "mm-modem-helpers-sierra.h"

GList *
mm_sierra_parse_scact_read_response (const gchar  *reply,
                                     GError      **error)
{
    GError     *inner_error = NULL;
    GRegex     *r;
    GMatchInfo *match_info;
    GList      *list;

    if (!reply || !reply[0])
        /* Nothing configured, all done */
        return NULL;

    list = NULL;
    r = g_regex_new ("!SCACT:\\s*(\\d+),(\\d+)",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW, 0, &inner_error);
    g_assert (r);

    g_regex_match_full (r, reply, strlen (reply), 0, 0, &match_info, &inner_error);
    while (!inner_error && g_match_info_matches (match_info)) {
        MM3gppPdpContextActive *pdp_active;
        guint cid = 0;
        guint aux = 0;

        if (!mm_get_uint_from_match_info (match_info, 1, &cid)) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Couldn't parse CID from reply: '%s'",
                                       reply);
            break;
        }
        if (!mm_get_uint_from_match_info (match_info, 2, &aux) || (aux != 0 && aux != 1)) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Couldn't parse context status from reply: '%s'",
                                       reply);
            break;
        }

        pdp_active = g_slice_new0 (MM3gppPdpContextActive);
        pdp_active->cid = cid;
        pdp_active->active = (gboolean) aux;
        list = g_list_prepend (list, pdp_active);

        g_match_info_next (match_info, &inner_error);
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        mm_3gpp_pdp_context_active_list_free (list);
        g_propagate_error (error, inner_error);
        g_prefix_error (error, "Couldn't properly parse list of active/inactive PDP contexts. ");
        return NULL;
    }

    list = g_list_sort (list, (GCompareFunc) mm_3gpp_pdp_context_active_cmp);

    return list;
}
