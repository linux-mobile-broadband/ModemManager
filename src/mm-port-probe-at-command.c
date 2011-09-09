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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include <glib.h>

#include "mm-errors.h"
#include "mm-port-probe.h"
#include "mm-port-probe-at-command.h"

/* ---- AT probing ---- */

static gboolean
parse_at (const gchar *response,
          const GError *error,
          GValue *result,
          GError **result_error)
{
    if (error) {
        /* On timeout, request to retry */
        if (g_error_matches (error,
                             MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_RESPONSE_TIMEOUT))
            return FALSE; /* Retry */

        /* If error is not recognizable, request to abort */
        if (error->domain != MM_MOBILE_ERROR) {
            *result_error = g_error_copy (error);
            g_prefix_error (result_error,
                            "Couldn't parse AT reply. ");
            return FALSE;
        }

        /* If the modem returned a recognizable error,
         * it can do AT commands */
        g_value_init (result, G_TYPE_BOOLEAN);
        g_value_set_boolean (result, TRUE);
        return TRUE;
    }

    /* No error reported, valid AT port! */
    g_value_init (result, G_TYPE_BOOLEAN);
    g_value_set_boolean (result, TRUE);
    return TRUE;
}

static const MMPortProbeAtCommand at_probing[] = {
    { "AT",  parse_at },
    { "AT",  parse_at },
    { "AT",  parse_at },
    { NULL }
};

const MMPortProbeAtCommand *
mm_port_probe_at_command_get_probing (void)
{
    return at_probing;
}
