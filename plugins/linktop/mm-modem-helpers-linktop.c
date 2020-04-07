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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2016 Red Hat, Inc.
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "mm-modem-helpers.h"
#include "mm-modem-helpers-linktop.h"

/*****************************************************************************/

gboolean
mm_linktop_parse_cfun_query_current_modes (const gchar  *response,
                                           MMModemMode  *allowed,
                                           GError      **error)
{
    guint state;

    g_assert (allowed);

    if (!mm_3gpp_parse_cfun_query_response (response, &state, error))
        return FALSE;

    switch (state) {
    case LINKTOP_MODE_OFFLINE:
    case LINKTOP_MODE_LOW_POWER:
        *allowed = MM_MODEM_MODE_NONE;
        return TRUE;
    case LINKTOP_MODE_2G:
        *allowed = MM_MODEM_MODE_2G;
        return TRUE;
    case LINKTOP_MODE_3G:
        *allowed = MM_MODEM_MODE_3G;
        return TRUE;
    case LINKTOP_MODE_ANY:
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        return TRUE;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Unknown linktop +CFUN current mode: %u", state);
        return FALSE;
    }
}
