/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "errors.h"
#include <stdlib.h>
#include <string.h>

WmcError *
wmc_error_new (u_int32_t domain,
               u_int32_t code,
               const char *format,
               ...)
{
    WmcError *error;
    va_list args;
    int n;

    wmc_return_val_if_fail (format != NULL, NULL);
    wmc_return_val_if_fail (format[0] != '\0', NULL);

    error = malloc (sizeof (WmcError));
    wmc_assert (error != NULL);

    error->domain = domain;
    error->code = code;

    va_start (args, format);
    n = vasprintf (&error->message, format, args);
    va_end (args);

    if (n < 0) {
        free (error);
        return NULL;
    }

    return error;
}

void
wmc_error_set (WmcError **error,
               u_int32_t domain,
               u_int32_t code,
               const char *format,
               ...)
{
    va_list args;
    int n;

    if (error == NULL)
        return;
    wmc_return_if_fail (*error == NULL);
    wmc_return_if_fail (format[0] != '\0');

    *error = malloc (sizeof (WmcError));
    wmc_assert (*error != NULL);

    (*error)->domain = domain;
    (*error)->code = code;

    va_start (args, format);
    n = vasprintf (&(*error)->message, format, args);
    va_end (args);

    if (n < 0) {
        free (*error);
        *error = NULL;
    }
}

static void
free_error (WmcError *error)
{
    if (error) {
        if (error->message)
            free (error->message);
        memset (error, 0, sizeof (*error));
        free (error);
    }
}

void
wmc_clear_error (WmcError **error)
{
    if (error) {
        free_error (*error);
        *error = NULL;
    }
}

void
wmc_free_error (WmcError *error)
{
    free_error (error);
}

