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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#define _GNU_SOURCE  /* for strcasestr */
#include <string.h>

#include <glib.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-log.h"
#include "mm-port-probe.h"
#include "mm-port-probe-at.h"
#include "mm-serial-parsers.h"

/* ---- AT probing ---- */

gboolean
mm_port_probe_response_processor_is_at (const gchar *command,
                                        const gchar *response,
                                        gboolean last_command,
                                        const GError *error,
                                        GVariant **result,
                                        GError **result_error)
{
    if (error) {
        /* Timeout errors are the only ones not fatal;
         * they will just go on to the next command. */
        if (g_error_matches (error,
                             MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            return FALSE;
        }

        /* If error is NOT known by the parser, or if the error is actually
         * the generic parsing filter error, request to abort */
        if (!mm_serial_parser_v1_is_known_error (error) ||
            g_error_matches (error,
                             MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_PARSE_FAILED)) {
            *result_error = g_error_copy (error);
            g_prefix_error (result_error,
                            "Fatal error parsing AT reply: ");
            return FALSE;
        }

        /* If the modem returned a recognizable error,
         * it can do AT commands */
        *result = g_variant_new_boolean (TRUE);
        return TRUE;
    }

    /* No error reported, valid AT port! */
    *result = g_variant_new_boolean (TRUE);
    return TRUE;
}

/* ---- String probing ---- */

gboolean
mm_port_probe_response_processor_string (const gchar *command,
                                         const gchar *response,
                                         gboolean last_command,
                                         const GError *error,
                                         GVariant **result,
                                         GError **result_error)
{
    gchar *str;

    if (error)
        /* Try with the next command, if any */
        return FALSE;

    str = g_strstrip (g_strdelimit (g_strdup (response), "\r\n", ' '));
    *result = g_variant_new_string (str);
    g_free (str);

    return TRUE;
}
